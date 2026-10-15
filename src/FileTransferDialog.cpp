#include "FileTransferDialog.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QDir>

#include <algorithm>

// Yerel islem hatalarinda Windows sistem kodunu (orn.145) okumak icin.
#ifdef Q_OS_WIN
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace {

QString formatSize(quint64 bytes)
{
    if (bytes < 1024ull)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024ull * 1024ull)
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < 1024ull * 1024ull * 1024ull)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString modifiedText(const QDateTime& time)
{
    if (!time.isValid())
        return {};
    return time.toLocalTime().toString(QStringLiteral("dd.MM.yyyy hh:mm"));
}

// Item veri rolleri
constexpr int RoleName = Qt::UserRole;
constexpr int RoleIsDir = Qt::UserRole + 1;
constexpr int RoleSize = Qt::UserRole + 2;
constexpr int RoleModified = Qt::UserRole + 3;
constexpr int RoleFullPath = Qt::UserRole + 4;

} // namespace

FileTransferDialog::FileTransferDialog(VncSession* session, const QString& serverTitle,
                                       QWidget* parent)
    : QDialog(parent)
    , m_session(session)
{
    setWindowTitle(tr("Dosya Transferi — %1").arg(serverTitle));
    setModal(true);
    resize(900, 560);
    setMinimumSize(760, 460);

    // ---- pano olusturma yardimcisi ------------------------------------------
    auto createPane = [this](QTreeWidget*& list) {
        list = new QTreeWidget(this);
        list->setColumnCount(3);
        list->setHeaderLabels({tr("Ad"), tr("Boyut"), tr("Değişiklik")});
        list->setRootIsDecorated(false);
        list->setAlternatingRowColors(true);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setSortingEnabled(false);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        return list;
    };

    // ---- sol pano: yerel bilgisayar ----------------------------------------
    auto* localTitle = new QLabel(tr("Yerel Bilgisayar (İstemci)"), this);
    localTitle->setStyleSheet(QStringLiteral("font-weight: bold;"));

    m_localPathEdit = new QLineEdit(this);
    m_localPathEdit->setPlaceholderText(tr("Yerel dizin, örn. C:/Kullanicilar"));
    connect(m_localPathEdit, &QLineEdit::returnPressed, this, [this] {
        navigateLocal(m_localPathEdit->text().trimmed());
    });

    m_localUp = new QPushButton(QStringLiteral("Üst"), this);
    connect(m_localUp, &QPushButton::clicked, this, &FileTransferDialog::localUp);

    auto* localPathRow = new QHBoxLayout;
    localPathRow->addWidget(m_localPathEdit, 1);
    localPathRow->addWidget(m_localUp);

    createPane(m_localList);
    connect(m_localList, &QTreeWidget::itemDoubleClicked, this,
            &FileTransferDialog::onLocalActivated);
    connect(m_localList, &QTreeWidget::customContextMenuRequested, this,
            &FileTransferDialog::onLocalMenu);

    m_localRename = new QPushButton(tr("Yeniden adlandır"), this);
    m_localMkdir = new QPushButton(tr("Klasör oluştur"), this);
    m_localRemove = new QPushButton(tr("Sil"), this);
    m_localRefresh = new QPushButton(tr("Yenile"), this);
    connect(m_localRename, &QPushButton::clicked, this, &FileTransferDialog::renameLocal);
    connect(m_localMkdir, &QPushButton::clicked, this, &FileTransferDialog::mkdirLocal);
    connect(m_localRemove, &QPushButton::clicked, this, &FileTransferDialog::removeLocal);
    connect(m_localRefresh, &QPushButton::clicked, this, &FileTransferDialog::refreshLocal);

    auto* localButtonRow = new QHBoxLayout;
    localButtonRow->addWidget(m_localRename);
    localButtonRow->addWidget(m_localMkdir);
    localButtonRow->addWidget(m_localRemove);
    localButtonRow->addStretch(1);
    localButtonRow->addWidget(m_localRefresh);

    auto* leftColumn = new QVBoxLayout;
    leftColumn->addWidget(localTitle);
    leftColumn->addLayout(localPathRow);
    leftColumn->addWidget(m_localList, 1);
    leftColumn->addLayout(localButtonRow);

    // ---- orta kolon: aktarma dugmeleri --------------------------------------
    m_uploadButton = new QPushButton(QStringLiteral(">>"), this);
    m_downloadButton = new QPushButton(QStringLiteral("<<"), this);
    m_uploadButton->setToolTip(tr("Seçili dosyayı sunucuya yükle"));
    m_downloadButton->setToolTip(tr("Seçili dosyayı bu bilgisayara indir"));
    m_uploadButton->setFixedWidth(48);
    m_downloadButton->setFixedWidth(48);
    connect(m_uploadButton, &QPushButton::clicked, this, &FileTransferDialog::uploadSelected);
    connect(m_downloadButton, &QPushButton::clicked, this,
            &FileTransferDialog::downloadSelected);

    auto* midColumn = new QVBoxLayout;
    midColumn->addStretch(1);
    midColumn->addWidget(m_uploadButton);
    midColumn->addWidget(m_downloadButton);
    midColumn->addStretch(1);

    // ---- sag pano: uzak bilgisayar ------------------------------------------
    auto* remoteTitle = new QLabel(tr("Uzak Bilgisayar (Sunucu)"), this);
    remoteTitle->setStyleSheet(QStringLiteral("font-weight: bold;"));

    m_remotePathEdit = new QLineEdit(this);
    m_remotePathEdit->setPlaceholderText(tr("Uzak dizin, örn. C:/Kullanicilar"));
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this, [this] {
        navigateRemote(m_remotePathEdit->text().trimmed());
    });

    m_remoteUp = new QPushButton(QStringLiteral("Üst"), this);
    connect(m_remoteUp, &QPushButton::clicked, this, &FileTransferDialog::remoteUp);

    auto* remotePathRow = new QHBoxLayout;
    remotePathRow->addWidget(m_remotePathEdit, 1);
    remotePathRow->addWidget(m_remoteUp);

    createPane(m_remoteList);
    connect(m_remoteList, &QTreeWidget::itemDoubleClicked, this,
            &FileTransferDialog::onRemoteActivated);
    connect(m_remoteList, &QTreeWidget::customContextMenuRequested, this,
            &FileTransferDialog::onRemoteMenu);

    m_remoteRename = new QPushButton(tr("Yeniden adlandır"), this);
    m_remoteMkdir = new QPushButton(tr("Klasör oluştur"), this);
    m_remoteRemove = new QPushButton(tr("Sil"), this);
    m_remoteRefresh = new QPushButton(tr("Yenile"), this);
    connect(m_remoteRename, &QPushButton::clicked, this, &FileTransferDialog::renameRemote);
    connect(m_remoteMkdir, &QPushButton::clicked, this, &FileTransferDialog::mkdirRemote);
    connect(m_remoteRemove, &QPushButton::clicked, this, &FileTransferDialog::removeRemote);
    connect(m_remoteRefresh, &QPushButton::clicked, this, &FileTransferDialog::refreshRemote);

    auto* remoteButtonRow = new QHBoxLayout;
    remoteButtonRow->addWidget(m_remoteRename);
    remoteButtonRow->addWidget(m_remoteMkdir);
    remoteButtonRow->addWidget(m_remoteRemove);
    remoteButtonRow->addStretch(1);
    remoteButtonRow->addWidget(m_remoteRefresh);

    auto* rightColumn = new QVBoxLayout;
    rightColumn->addWidget(remoteTitle);
    rightColumn->addLayout(remotePathRow);
    rightColumn->addWidget(m_remoteList, 1);
    rightColumn->addLayout(remoteButtonRow);

    // ---- alt satir: ilerleme + iptal ----------------------------------------
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setFixedHeight(14);

    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);

    m_cancelButton = new QPushButton(tr("İptal"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, &FileTransferDialog::cancelTransfer);

    auto* bottomLayout = new QVBoxLayout;
    bottomLayout->addWidget(m_progress);
    bottomLayout->addWidget(m_status);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->addLayout(bottomLayout, 1);
    bottomRow->addWidget(m_cancelButton);

    // ---- genel duzen ---------------------------------------------------------
    auto* mainRow = new QHBoxLayout;
    mainRow->addLayout(leftColumn, 1);
    mainRow->addSpacing(8);
    mainRow->addLayout(midColumn);
    mainRow->addSpacing(8);
    mainRow->addLayout(rightColumn, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(mainRow, 1);
    layout->addSpacing(6);
    layout->addLayout(bottomRow);

    // ---- oturum sinyalleri ----------------------------------------------------
    if (m_session) {
        connect(m_session, &VncSession::ftListingReceived, this,
                &FileTransferDialog::onListing);
        connect(m_session, &VncSession::ftDrivesReceived, this,
                &FileTransferDialog::onDrives);
        connect(m_session, &VncSession::ftProgress, this, &FileTransferDialog::onProgress);
        connect(m_session, &VncSession::ftFinished, this, &FileTransferDialog::onFinish);
        connect(m_session, &QObject::destroyed, this, [this] {
            m_session = nullptr;
            m_refreshRemote = m_refreshLocal = false;
            setBusy(false);
            setStatus(tr("Oturum kapandı"), true);
        });
    } else {
        setStatus(tr("Oturum yok"), true);
    }

    // ---- acilis durumu --------------------------------------------------------
    setBusy(false);
    navigateLocal(QStringLiteral("/"));

    QSettings settings;
    const QString startPath = settings.value(QStringLiteral("fileTransfer/startPath"),
                                             QStringLiteral("C:/")).toString();
    if (m_session)
        navigateRemote(startPath);
}

// ---------------------------------------------------------------------------
// yol yardimcilari
// ---------------------------------------------------------------------------

QString FileTransferDialog::joinPath(const QString& dir, const QString& name)
{
    if (dir.isEmpty())
        return name;
    if (dir == QLatin1String("/"))
        return name;
    if (dir.endsWith(QLatin1Char('/')))
        return dir + name;
    return dir + QLatin1Char('/') + name;
}

QString FileTransferDialog::parentPath(const QString& path)
{
    if (path == QLatin1String("/"))
        return QStringLiteral("/");

    QString p = path;
    while (p.size() > 3 && p.endsWith(QLatin1Char('/')))
        p.chop(1);

    const int idx = p.lastIndexOf(QLatin1Char('/'));
    if (idx < 0)
        return QStringLiteral("/");            // "C:" -> surucu listesi
    if (idx <= 2)
        return p.left(2) + QLatin1Char('/');   // "C:/Kullanicilar" -> "C:/"
    return p.left(idx);                        // "C:/a/b" -> "C:/a"
}

QString FileTransferDialog::displayName(const QString& path)
{
    QString p = path;
    if (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p;
}

// ---------------------------------------------------------------------------
// gezinme
// ---------------------------------------------------------------------------

QVector<VncSession::FtEntry> FileTransferDialog::localEntries(const QString& path) const
{
    QVector<VncSession::FtEntry> entries;

    if (path == QLatin1String("/")) {
        const QFileInfoList drives = QDir::drives();
        for (const QFileInfo& info : drives) {
            VncSession::FtEntry entry;
            entry.name = displayName(info.absoluteFilePath());
            entry.isDir = true;
            entries.append(entry);
        }
        return entries;
    }

    const QDir dir(path);
    const QFileInfoList list = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::Drives | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name);

    for (const QFileInfo& info : list) {
        VncSession::FtEntry entry;
        entry.name = info.fileName();
        entry.isDir = info.isDir();
        entry.size = quint64(info.size());
        entry.modified = info.lastModified();
        entries.append(entry);
    }
    return entries;
}

void FileTransferDialog::navigateLocal(const QString& path)
{
    if (m_busy) {
        setStatus(tr("Devam eden bir işlem var"), true);
        return;
    }

    if (path != QLatin1String("/")) {
        QString normalized = QDir::fromNativeSeparators(path.trimmed());
        while (normalized.size() > 3 && normalized.endsWith(QLatin1Char('/')))
            normalized.chop(1);

        if (!QDir(normalized).exists()) {
            setStatus(tr("Dizin bulunamadı: %1").arg(normalized), true);
            return;
        }
        m_localPath = normalized;
    } else {
        m_localPath = QStringLiteral("/");
    }

    m_localPathEdit->setText(m_localPath);
    const auto entries = localEntries(m_localPath);
    fillPane(Side::Local, entries);
    setStatus(tr("Yerel: %1 — %2 öğe").arg(displayName(m_localPath)).arg(entries.size()));
}

void FileTransferDialog::navigateRemote(const QString& path)
{
    if (!m_session) {
        setStatus(tr("Oturum yok"), true);
        return;
    }
    if (m_busy) {
        setStatus(tr("Devam eden bir işlem var"), true);
        return;
    }

    QString normalized = path.trimmed();
    // Gomulu NUL'lar (eski protokol hatasi birikimleri) ve Windows'un yol
    // sonundan yok saydigi noktalar temizlenir.
    normalized.remove(QChar(0));
    while (normalized.length() > 3 && normalized.endsWith(QLatin1Char('.')))
        normalized.chop(1);
    if (normalized.isEmpty()) {
        setStatus(tr("Geçerli bir yol yazın"), true);
        return;
    }

    if (normalized == QLatin1String("/")) {
        m_remotePath = QStringLiteral("/");
        m_remotePathEdit->setText(QStringLiteral("/"));
        setBusy(true);
        setStatus(tr("Sürücüler alınıyor…"));
        m_session->ftListDrives();
        return;
    }

    m_remotePath = normalized;
    m_remotePathEdit->setText(m_remotePath);

    QSettings settings;
    settings.setValue(QStringLiteral("fileTransfer/startPath"), m_remotePath);

    setBusy(true);
    setStatus(tr("Uzak listeleniyor: %1").arg(m_remotePath));
    m_session->ftListDirectory(m_remotePath);
}

void FileTransferDialog::localUp()
{
    if (m_busy)
        return;
    navigateLocal(parentPath(m_localPath));
}

void FileTransferDialog::remoteUp()
{
    if (m_busy)
        return;
    navigateRemote(parentPath(m_remotePath));
}

void FileTransferDialog::refreshLocal()
{
    if (m_busy)
        return;
    navigateLocal(m_localPath);
}

void FileTransferDialog::refreshRemote()
{
    if (m_busy)
        return;
    if (m_session)
        navigateRemote(m_remotePath);
    else
        setStatus(tr("Oturum yok"), true);
}

// ---------------------------------------------------------------------------
// listeler
// ---------------------------------------------------------------------------

void FileTransferDialog::fillPane(Side side, const QVector<VncSession::FtEntry>& entries)
{
    QTreeWidget* pane = (side == Side::Local) ? m_localList : m_remoteList;
    const QString& basePath = (side == Side::Local) ? m_localPath : m_remotePath;

    QVector<VncSession::FtEntry> dirs;
    QVector<VncSession::FtEntry> files;
    dirs.reserve(entries.size());
    for (const VncSession::FtEntry& entry : entries) {
        if (entry.name == QLatin1String(".") || entry.name == QLatin1String(".."))
            continue;
        if (entry.isDir)
            dirs.append(entry);
        else
            files.append(entry);
    }

    const auto byName = [](const VncSession::FtEntry& a, const VncSession::FtEntry& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    pane->setUpdatesEnabled(false);
    pane->clear();

    const bool isRoot = (basePath == QLatin1String("/"));

    // ".." satiri (kok disinda)
    if (!isRoot) {
        auto* up = new QTreeWidgetItem(pane);
        up->setText(0, QStringLiteral(".."));
        up->setData(0, RoleName, QStringLiteral(".."));
        up->setData(0, RoleIsDir, true);
        up->setData(0, RoleFullPath, parentPath(basePath));
        up->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
    }

    const auto append = [pane, &basePath, &isRoot](const VncSession::FtEntry& entry) {
        auto* item = new QTreeWidgetItem(pane);
        item->setText(0, entry.isDir ? entry.name + QLatin1Char('/') : entry.name);
        item->setData(0, RoleName, entry.name);
        item->setData(0, RoleIsDir, entry.isDir);
        item->setData(0, RoleSize, QVariant::fromValue(entry.size));
        item->setData(0, RoleModified, entry.modified);
        item->setData(0, RoleFullPath,
                      isRoot ? entry.name + QLatin1Char('/')
                             : (basePath.endsWith(QLatin1Char('/'))
                                    ? basePath + entry.name
                                    : basePath + QLatin1Char('/') + entry.name));
        item->setText(1, entry.isDir ? QString() : formatSize(entry.size));
        item->setText(2, modifiedText(entry.modified));
    };

    for (const VncSession::FtEntry& entry : dirs)
        append(entry);
    for (const VncSession::FtEntry& entry : files)
        append(entry);

    pane->setUpdatesEnabled(true);
}

void FileTransferDialog::onListing(const QString& path,
                                   const QVector<VncSession::FtEntry>& entries,
                                   const QString& error)
{
    m_busy = false;
    setBusy(false);

    if (!error.isEmpty()) {
        setStatus(error, true);
        return;
    }

    if (m_remotePath != path && path != QLatin1String("/"))
        m_remotePath = path;

    fillPane(Side::Remote, entries);
    setStatus(tr("Uzak: %1 — %2 öğe").arg(displayName(m_remotePath)).arg(entries.size()));
}

void FileTransferDialog::onDrives(const QStringList& drives)
{
    m_busy = false;
    setBusy(false);

    m_drives = drives;
    if (m_remotePath != QLatin1String("/"))
        return;

    QVector<VncSession::FtEntry> entries;
    for (const QString& drive : drives) {
        VncSession::FtEntry entry;
        entry.name = displayName(drive);   // "C:/" -> "C:"
        entry.isDir = true;
        entries.append(entry);
    }

    if (entries.isEmpty()) {
        setStatus(tr("Sürücü listesi alınamadı"), true);
        return;
    }

    fillPane(Side::Remote, entries);
    setStatus(tr("Sürücüler: %1").arg(drives.join(QStringLiteral(", "))));
}

// ---------------------------------------------------------------------------
// aktarma
// ---------------------------------------------------------------------------

void FileTransferDialog::uploadSelected()
{
    if (!m_session || m_busy) {
        setStatus(tr("Devam eden bir işlem var"), true);
        return;
    }
    if (!hasSelection(Side::Local)) {
        setStatus(tr("Soldan bir dosya seçin"), true);
        return;
    }
    if (selectedIsDir(Side::Local)) {
        setStatus(tr("Klasör aktarımı desteklenmiyor; yalnızca dosya seçilebilir"), true);
        return;
    }
    if (m_remotePath == QLatin1String("/")) {
        setStatus(tr("Önce sağda bir klasöre (sürücüye) girin"), true);
        return;
    }

    const QString localFull = selectedPath(Side::Local);
    const QString remoteFull = joinPath(m_remotePath, selectedName(Side::Local));

    m_refreshRemote = true;
    setBusy(true);
    setStatus(tr("Yükleniyor: %1 → %2").arg(localFull, remoteFull));
    m_session->ftUpload(localFull, remoteFull);
}

void FileTransferDialog::downloadSelected()
{
    if (!m_session || m_busy) {
        setStatus(tr("Devam eden bir işlem var"), true);
        return;
    }
    if (!hasSelection(Side::Remote)) {
        setStatus(tr("Sağdan bir dosya seçin"), true);
        return;
    }
    if (selectedIsDir(Side::Remote)) {
        setStatus(tr("Klasör aktarımı desteklenmiyor; yalnızca dosya seçilebilir"), true);
        return;
    }
    if (m_localPath == QLatin1String("/")) {
        setStatus(tr("Önce solda bir klasöre (sürücüye) girin"), true);
        return;
    }

    const QString remoteFull = selectedPath(Side::Remote);
    const QString localFull = joinPath(m_localPath, selectedName(Side::Remote));

    m_refreshLocal = true;
    setBusy(true);
    setStatus(tr("İndiriliyor: %1 → %2").arg(remoteFull, localFull));
    m_session->ftDownload(remoteFull, localFull);
}

void FileTransferDialog::cancelTransfer()
{
    if (m_session && m_busy) {
        setStatus(tr("İptal ediliyor…"));
        m_session->ftAbort();
    }
}

// ---------------------------------------------------------------------------
// yerel islemler
// ---------------------------------------------------------------------------

void FileTransferDialog::renameLocal()
{
    if (m_busy || !hasSelection(Side::Local)) {
        setStatus(tr("Önce bir öğe seçin"), true);
        return;
    }

    const QString oldName = selectedName(Side::Local);
    if (oldName == QLatin1String("..")) {
        setStatus(tr("Bu öğe yeniden adlandırılamaz"), true);
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Yeniden adlandır"),
                                                  tr("Yeni ad:"), QLineEdit::Normal,
                                                  oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    if (newName.contains(QLatin1Char('/'))) {
        setStatus(tr("Ad '/' içermemeli"), true);
        return;
    }

    const QString oldPath = selectedPath(Side::Local);
    const QString newPath = joinPath(m_localPath, newName);

    if (QDir().rename(oldPath, newPath)) {
        refreshLocal();
        setStatus(tr("Yeniden adlandırıldı: %1").arg(newName));
    } else {
        setStatus(tr("Yeniden adlandırılamadı: %1").arg(oldName), true);
    }
}

void FileTransferDialog::mkdirLocal()
{
    if (m_busy) {
        setStatus(tr("Devam eden bir işlem var"), true);
        return;
    }
    if (m_localPath == QLatin1String("/")) {
        setStatus(tr("Önce bir sürücüye girin"), true);
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Klasör oluştur"),
                                               tr("Klasör adı:"), QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.contains(QLatin1Char('/'))) {
        setStatus(tr("Ad '/' içermemeli"), true);
        return;
    }

    if (QDir(m_localPath).mkdir(name)) {
        refreshLocal();
        setStatus(tr("Klasör oluşturuldu: %1").arg(name));
    } else {
        setStatus(tr("Klasör oluşturulamadı: %1").arg(name), true);
    }
}

void FileTransferDialog::removeLocal()
{
    if (m_busy || !hasSelection(Side::Local)) {
        setStatus(tr("Önce bir öğe seçin"), true);
        return;
    }

    const QString name = selectedName(Side::Local);
    if (name == QLatin1String("..")) {
        setStatus(tr("Bu öğe silinemez"), true);
        return;
    }

    const QString path = selectedPath(Side::Local);
    const bool isDir = selectedIsDir(Side::Local);

    const auto answer = QMessageBox::question(
        this, tr("Sil"),
        tr("\"%1\" silinecek. Bu işlem geri alınamaz.\n\nSilinsin mi?").arg(path),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    const bool done = isDir ? QDir().rmdir(path) : QFile::remove(path);
    if (done) {
        refreshLocal();
        setStatus(tr("Silindi: %1").arg(name));
        return;
    }

    // Windows sistem kodu (orn.145 = klasor dolu) ayni ceviri katmanindan
    // gecirilir; tanimayan bir kod durumda eski mesajlar korunur.
    const QString systemReason = translateSystemError(lastSystemErrorText());
    QString message;
    if (isDir) {
        message = systemReason.isEmpty() ? tr("Klasör silinemedi (boş olmalı): %1").arg(name)
                                         : tr("Klasör silinemedi: %1 — %2").arg(name, systemReason);
    } else {
        message = systemReason.isEmpty() ? tr("Dosya silinemedi: %1").arg(name)
                                         : tr("Dosya silinemedi: %1 — %2").arg(name, systemReason);
    }
    setStatus(message, true);
}

// ---------------------------------------------------------------------------
// uzak islemler
// ---------------------------------------------------------------------------

void FileTransferDialog::renameRemote()
{
    if (!m_session || m_busy || !hasSelection(Side::Remote)) {
        setStatus(tr("Önce sağdan bir öğe seçin"), true);
        return;
    }

    const QString oldName = selectedName(Side::Remote);
    if (oldName == QLatin1String("..")) {
        setStatus(tr("Bu öğe yeniden adlandırılamaz"), true);
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Yeniden adlandır"),
                                                  tr("Yeni ad:"), QLineEdit::Normal,
                                                  oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    if (newName.contains(QLatin1Char('/'))) {
        setStatus(tr("Ad '/' içermemeli"), true);
        return;
    }

    m_refreshRemote = true;
    setBusy(true);
    setStatus(tr("Adlandırılıyor: %1").arg(newName));
    m_session->ftRename(selectedPath(Side::Remote), joinPath(m_remotePath, newName));
}

void FileTransferDialog::mkdirRemote()
{
    if (!m_session || m_busy) {
        setStatus(tr("Devam eden bir işlem var"), true);
        return;
    }
    if (m_remotePath == QLatin1String("/")) {
        setStatus(tr("Önce sağda bir klasöre (sürücüye) girin"), true);
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Klasör oluştur"),
                                               tr("Klasör adı:"), QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.contains(QLatin1Char('/'))) {
        setStatus(tr("Ad '/' içermemeli"), true);
        return;
    }

    m_refreshRemote = true;
    setBusy(true);
    setStatus(tr("Klasör oluşturuluyor: %1").arg(name));
    m_session->ftMkdir(joinPath(m_remotePath, name));
}

void FileTransferDialog::removeRemote()
{
    if (!m_session || m_busy || !hasSelection(Side::Remote)) {
        setStatus(tr("Önce sağdan bir öğe seçin"), true);
        return;
    }

    const QString name = selectedName(Side::Remote);
    if (name == QLatin1String("..")) {
        setStatus(tr("Bu öğe silinemez"), true);
        return;
    }

    const QString path = selectedPath(Side::Remote);
    if (selectedIsDir(Side::Remote)) {
        // Dolu klasör silmeyi sunucu reddedecektir; islem öncesi bilgilendir.
        const auto answer = QMessageBox::question(
            this, tr("Klasörü sil"),
            tr("Klasör silinecek: %1. Klasör boş değilse sunucu reddedecek. "
               "Devam edilsin mi?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    } else {
        const auto answer = QMessageBox::question(
            this, tr("Uzak öğeyi sil"),
            tr("Sunucudan silinecek:\n%1\n\nBu işlem geri alınamaz. Silinsin mi?").arg(path),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_refreshRemote = true;
    setBusy(true);
    setStatus(tr("Siliniyor: %1").arg(name));
    m_session->ftRemove(path);
}

// ---------------------------------------------------------------------------
// secim / pano
// ---------------------------------------------------------------------------

bool FileTransferDialog::hasSelection(Side side) const
{
    QTreeWidget* pane = (side == Side::Local) ? m_localList : m_remoteList;
    return pane->currentItem() != nullptr;
}

QString FileTransferDialog::selectedPath(Side side) const
{
    QTreeWidget* pane = (side == Side::Local) ? m_localList : m_remoteList;
    QTreeWidgetItem* item = pane->currentItem();
    return item ? item->data(0, RoleFullPath).toString() : QString();
}

QString FileTransferDialog::selectedName(Side side) const
{
    QTreeWidget* pane = (side == Side::Local) ? m_localList : m_remoteList;
    QTreeWidgetItem* item = pane->currentItem();
    return item ? item->data(0, RoleName).toString() : QString();
}

bool FileTransferDialog::selectedIsDir(Side side) const
{
    QTreeWidget* pane = (side == Side::Local) ? m_localList : m_remoteList;
    QTreeWidgetItem* item = pane->currentItem();
    return item ? item->data(0, RoleIsDir).toBool() : false;
}

void FileTransferDialog::copyToClipboard(Side side, bool nameOnly)
{
    if (!hasSelection(side)) {
        setStatus(tr("Önce bir öğe seçin"), true);
        return;
    }

    const QString text = nameOnly ? selectedName(side) : selectedPath(side);
    QGuiApplication::clipboard()->setText(text);
    setStatus(tr("Panoya kopyalandı: %1").arg(text));
}

void FileTransferDialog::onLocalActivated(QTreeWidgetItem* item, int)
{
    if (!item)
        return;
    if (selectedIsDir(Side::Local))
        navigateLocal(item->data(0, RoleFullPath).toString());
    else
        uploadSelected();
}

void FileTransferDialog::onRemoteActivated(QTreeWidgetItem* item, int)
{
    if (!item)
        return;
    if (selectedIsDir(Side::Remote))
        navigateRemote(item->data(0, RoleFullPath).toString());
    else
        downloadSelected();
}

void FileTransferDialog::onLocalMenu(const QPoint& pos)
{
    showMenu(Side::Local, pos);
}

void FileTransferDialog::onRemoteMenu(const QPoint& pos)
{
    showMenu(Side::Remote, pos);
}

void FileTransferDialog::showMenu(Side side, const QPoint& pos)
{
    QTreeWidget* pane = (side == Side::Local) ? m_localList : m_remoteList;
    QTreeWidgetItem* item = pane->itemAt(pos);
    if (item)
        pane->setCurrentItem(item);

    QMenu menu(this);
    QAction* copyPath = menu.addAction(tr("Yolunu panoya kopyala"));
    QAction* copyName = menu.addAction(tr("Adını panoya kopyala"));
    menu.addSeparator();

    QAction* transfer = nullptr;
    if (item && !selectedIsDir(side)) {
        transfer = menu.addAction(side == Side::Local ? tr("Sunucuya yükle…")
                                                      : tr("İndir…"));
    }

    QAction* rename = menu.addAction(tr("Yeniden adlandır…"));
    QAction* mkdir = menu.addAction(tr("Klasör oluştur…"));
    QAction* remove = menu.addAction(tr("Sil…"));
    menu.addSeparator();
    QAction* refresh = menu.addAction(tr("Yenile"));

    QAction* chosen = menu.exec(pane->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == copyPath)
        copyToClipboard(side, false);
    else if (chosen == copyName)
        copyToClipboard(side, true);
    else if (transfer && chosen == transfer) {
        if (side == Side::Local)
            uploadSelected();
        else
            downloadSelected();
    } else if (chosen == rename) {
        if (side == Side::Local)
            renameLocal();
        else
            renameRemote();
    } else if (chosen == mkdir) {
        if (side == Side::Local)
            mkdirLocal();
        else
            mkdirRemote();
    } else if (chosen == remove) {
        if (side == Side::Local)
            removeLocal();
        else
            removeRemote();
    } else if (chosen == refresh) {
        if (side == Side::Local)
            refreshLocal();
        else
            refreshRemote();
    }
}

// ---------------------------------------------------------------------------
// oturum sinyalleri
// ---------------------------------------------------------------------------

void FileTransferDialog::onProgress(qint64 done, qint64 total)
{
    if (total > 0) {
        m_progress->setRange(0, 1000);
        m_progress->setValue(int(done * 1000 / total));
        setStatus(tr("%1 / %2  (%3%)")
                      .arg(formatSize(done))
                      .arg(formatSize(total))
                      .arg(int(done * 100 / total)));
    } else {
        m_progress->setRange(0, 0);
        setStatus(tr("%1 gönderiliyor…").arg(formatSize(done)));
    }
}

void FileTransferDialog::onFinish(bool ok, const QString& message)
{
    const bool refreshRemote = m_refreshRemote;
    const bool refreshLocal = m_refreshLocal;
    m_refreshRemote = false;
    m_refreshLocal = false;

    m_busy = false;
    setBusy(false);

    m_progress->setRange(0, 1000);
    m_progress->setValue(ok ? 1000 : 0);
    setStatus(message, !ok);

    if (!ok)
        return;

    if (refreshLocal)
        navigateLocal(m_localPath);

    if (refreshRemote && m_session) {
        setBusy(true);
        if (m_remotePath == QLatin1String("/"))
            m_session->ftListDrives();
        else
            m_session->ftListDirectory(m_remotePath);
    }
}

// ---------------------------------------------------------------------------
// durum
// ---------------------------------------------------------------------------

QString FileTransferDialog::translateSystemError(const QString& text)
{
    // "Error 145", "error kodu 32", "Hata: 5" kaliplari buyuk/kucuk harf
    // duyarsiz aranir; eslesmeyen metin oldugu gibi geri doner.
    static const QRegularExpression pattern(
        QStringLiteral("\\b(?:error|hata|kod|code)\\b(?:\\s*(?:kodu|code))?"
                       "\\s*[:#]?\\s*(\\d{1,5})\\b"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return text;

    switch (match.captured(1).toInt()) {
    case 2:
    case 3:
        return tr("Dosya veya klasör bulunamadı.");
    case 5:
        return tr("Erişim reddedildi. (yetki)");
    case 17:
        return tr("Aynı adla başka bir dosya var.");
    case 32:
        return tr("Dosya başka bir işlem tarafından kullanılıyor.");
    case 80:
    case 183:
        return tr("Hedef zaten var.");
    case 145:
        return tr("Klasör boş değil; önce içeriğini silin.");
    default:
        return text;    // tanimayan kod: oldugu gibi goster
    }
}

QString FileTransferDialog::lastSystemErrorText()
{
#ifdef Q_OS_WIN
    const DWORD code = ::GetLastError();
    if (code != 0) {
        const QString raw = QStringLiteral("Hata %1").arg(code);
        // Tanimayan bir kod durum cubuguna yanlis bilgi yazmasin diye
        // yalnizca cevirebilen kodlar geri dondurulur.
        if (translateSystemError(raw) != raw)
            return raw;
    }
#endif
    return {};
}

void FileTransferDialog::setBusy(bool busy)
{
    m_busy = busy;

    const bool hasSession = (m_session != nullptr);

    // Yerel pano her hâlde kullanilabilir (oturum gerekmez)
    m_localPathEdit->setEnabled(!busy);
    m_localUp->setEnabled(!busy);
    m_localRename->setEnabled(!busy);
    m_localMkdir->setEnabled(!busy);
    m_localRemove->setEnabled(!busy);
    m_localRefresh->setEnabled(!busy);
    m_localList->setEnabled(!busy);

    // Uzak pano oturuma bagli
    m_remotePathEdit->setEnabled(!busy && hasSession);
    m_remoteUp->setEnabled(!busy && hasSession);
    m_remoteRename->setEnabled(!busy && hasSession);
    m_remoteMkdir->setEnabled(!busy && hasSession);
    m_remoteRemove->setEnabled(!busy && hasSession);
    m_remoteRefresh->setEnabled(!busy && hasSession);
    m_remoteList->setEnabled(hasSession);

    m_uploadButton->setEnabled(!busy && hasSession);
    m_downloadButton->setEnabled(!busy && hasSession);
    m_cancelButton->setEnabled(busy && hasSession);

    if (busy)
        m_progress->setRange(0, 0);      // belirsiz isaret
    else
        m_progress->setRange(0, 1000);
}

void FileTransferDialog::setStatus(const QString& text, bool error)
{
    m_status->setStyleSheet(error ? QStringLiteral("color: #c0392b;")
                                  : QStringLiteral("color: #2e9e4f;"));
    // Sunucudan ya da yerel islemden gelen hata metinleri anlasilir
    // Turkceye cevrilir (orn. "Error 145: ..."); diger metinler degismez.
    m_status->setText(error ? translateSystemError(text) : text);
}
