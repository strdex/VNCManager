#include "MainWindow.h"
#include "ConnectionDialog.h"
#include "ConnectionInfoDialog.h"
#include "ConnectionStore.h"
#include "FileTransferDialog.h"
#include "VncView.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QKeySequence>
#include <QMenu>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("xVNC Manager"));
    resize(1100, 720);

    qDebug() << "step: buildUi";
    buildUi();
    qDebug() << "step: loadProfiles";
    loadProfiles();
    qDebug() << "step: refreshList";
    refreshList();
    qDebug() << "step: restore";

    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    qDebug() << "step: ctor bitti";
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    // ---- Sol panel: kayitli baglantilar + hizli baglan -----------------------
    m_list = new QTreeWidget(this);
    m_list->setColumnCount(1);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onProfileDoubleClicked);
    connect(m_list, &QWidget::customContextMenuRequested, this, &MainWindow::onListContextMenu);

    auto* renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), m_list);
    connect(renameShortcut, &QShortcut::activated, this, &MainWindow::onRenameConnection);

    auto* quickLayout = new QHBoxLayout;
    m_quickHostEdit = new QLineEdit(this);
    m_quickHostEdit->setPlaceholderText(tr("IP veya bilgisayar adı (port: 5900)"));
    auto* quickButton = new QPushButton(tr("Bağlan"), this);
    quickLayout->addWidget(m_quickHostEdit);
    quickLayout->addWidget(quickButton);
    connect(quickButton, &QPushButton::clicked, this, &MainWindow::onQuickConnect);
    connect(m_quickHostEdit, &QLineEdit::returnPressed, this, &MainWindow::onQuickConnect);

    auto* newButton = new QPushButton(tr("Yeni"), this);
    auto* editButton = new QPushButton(tr("Düzenle"), this);
    auto* deleteButton = new QPushButton(tr("Sil"), this);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::onNewConnection);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::onEditConnection);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::onDeleteConnection);

    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->addWidget(newButton);
    buttonsLayout->addWidget(editButton);
    buttonsLayout->addWidget(deleteButton);

    auto* group = new QGroupBox(tr("Kayıtlı Sunucular"), this);
    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(m_list);
    groupLayout->addLayout(buttonsLayout);

    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(tr("Hızlı bağlan"), left));
    leftLayout->addLayout(quickLayout);
    leftLayout->addWidget(group);

    // ---- Sag panel: oturum sekmeleri ----------------------------------------
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* widget = m_tabs->widget(index);
        m_tabs->removeTab(index);
        delete widget;
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    auto* emptyLabel = new QLabel(tr("Bir sunucuya bağlanmak için:\n"
                                     "• listeden sunucuyu seçip çift tıklayın\n"
                                     "• veya üstteki araç çubuğundan “Bağlan” deyin"),
                                  this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet(QStringLiteral("color: gray;"));

    auto* placeholder = new QScrollArea(this);
    placeholder->setWidget(emptyLabel);
    placeholder->setWidgetResizable(true);
    m_tabs->addTab(placeholder, tr("Bağlantı yok"));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(left);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 820});
    setCentralWidget(splitter);

    // ---- Araç çubuğu ve eylemler --------------------------------------------
    auto* toolbar = addToolBar(tr("Ana araç çubuğu"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(20, 20));

    m_connectAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/connect.svg")),
                                         tr("Bağlan"), this, &MainWindow::onConnectCurrent);
    m_connectAction->setShortcut(QKeySequence(Qt::Key_F5));

    m_disconnectAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/disconnect.svg")),
                                            tr("Bağlantıyı kes"), this,
                                            &MainWindow::onDisconnectActive);
    m_disconnectAction->setToolTip(tr("Bağlantıyı kes ve sekmeyi kapat"));

    m_transferAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/transfer.svg")),
                                          tr("Dosya transferi"), this,
                                          &MainWindow::onFileTransfer);
    m_transferAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    m_transferAction->setToolTip(
        tr("Uzak sunucuda gezin, dosya indir/yükle (Ctrl+Shift+F)"));

    toolbar->addSeparator();

    m_cadAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/cad.svg")),
                                     tr("Ctrl+Alt+Del gönder"), this,
                                     &MainWindow::onSendCtrlAltDelete);
    m_cadAction->setToolTip(tr("Ctrl+Alt+Del tuşlarını sunucuya gönder (kilitli/ekran koruyucu ekranlar için)"));

    m_winAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/win.svg")),
                                     tr("Windows tuşu gönder"), this,
                                     &MainWindow::onSendWindowsKey);
    m_winAction->setToolTip(tr("Windows (Başlat) tuşunu sunucuya gönder"));

    toolbar->addSeparator();

    m_zoomOutAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-out.svg")),
                                         tr("Uzaklaştır"), this, &MainWindow::onZoomOut);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setToolTip(tr("Uzaklaştır (Ctrl+−, Ctrl+tekerlek)"));

    m_zoomLabel = new QLabel(toolbar);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(56);
    m_zoomLabel->setText(QStringLiteral("—"));
    toolbar->addWidget(m_zoomLabel);

    m_zoomInAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/zoom-in.svg")),
                                        tr("Yakınlaştır"), this, &MainWindow::onZoomIn);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setToolTip(tr("Yakınlaştır (Ctrl++, Ctrl+tekerlek)"));

    m_fitAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/fit.svg")),
                                     tr("Sığdır"), this, &MainWindow::onFitWindow);
    m_fitAction->setCheckable(true);
    m_fitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    m_fitAction->setToolTip(tr("Görüntüyü pencereye sığdır (Ctrl+0, çift tık)"));

    m_actualSizeAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/actual.svg")),
                                            tr("Tam görüntüle"), this, &MainWindow::onActualSize);
    m_actualSizeAction->setCheckable(true);
    m_actualSizeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    m_actualSizeAction->setToolTip(tr("Gerçek boyutta göster, 1:1 (Ctrl+1, çift tık)"));

    toolbar->addSeparator();

    m_viewOnlyAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/viewonly.svg")),
                                          tr("İzleme"), this, &MainWindow::onViewOnlyToggled);
    m_viewOnlyAction->setCheckable(true);
    m_viewOnlyAction->setToolTip(tr("İzleme modu: fare ve klavye sunucuya gönderilmez.\n"
                                    "Kontrol moduna geçmek için düğmeye tekrar basın."));

    m_cursorAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/cursor.svg")),
                                        tr("Uzak imleç"), this, &MainWindow::onRemoteCursorToggled);
    m_cursorAction->setCheckable(true);
    m_cursorAction->setChecked(true);
    m_cursorAction->setToolTip(tr("Sunucunun imlecini göster (yerel imleç gizlenir)"));

    toolbar->addSeparator();

    m_fullScreenAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/fullscreen.svg")),
                                            tr("Tam ekran"), this, &MainWindow::onToggleFullScreen);
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));

    m_infoAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/info.svg")),
                                      tr("Bağlantı bilgisi"), this,
                                      &MainWindow::onShowConnectionInfo);
    m_infoAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));

    m_closeTabAction = toolbar->addAction(QIcon(QStringLiteral(":/icons/close.svg")),
                                          tr("Sekmeyi kapat"), this, &MainWindow::onCloseActiveTab);
    m_closeTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));

    // ---- Menuler -------------------------------------------------------------
    QMenu* fileMenu = menuBar()->addMenu(tr("Dosya"));
    fileMenu->addAction(m_infoAction);
    fileMenu->addAction(m_transferAction);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Yeni bağlantı..."), QKeySequence(QStringLiteral("Ctrl+N")),
                        this, &MainWindow::onNewConnection);
    fileMenu->addAction(tr("Bağlantıyı düzenle..."), QKeySequence(QStringLiteral("Ctrl+E")),
                        this, &MainWindow::onEditConnection);
    fileMenu->addAction(tr("Bağlantıyı sil"), QKeySequence::Delete,
                        this, &MainWindow::onDeleteConnection);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Bağlantı listesini dışa aktar..."), this,
                        &MainWindow::onExportConnections);
    fileMenu->addAction(tr("Bağlantı listesini içe aktar..."), this,
                        &MainWindow::onImportConnections);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Çıkış"), QKeySequence(QStringLiteral("Ctrl+Q")),
                        qApp, &QApplication::quit);

    QMenu* viewMenu = menuBar()->addMenu(tr("Görünüm"));
    viewMenu->addAction(m_fullScreenAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_cursorAction);

    QMenu* keysMenu = menuBar()->addMenu(tr("Tuşlar"));
    keysMenu->addAction(m_cadAction);
    keysMenu->addAction(m_winAction);
    keysMenu->addSeparator();
    keysMenu->addAction(m_viewOnlyAction);

    QMenu* clipMenu = menuBar()->addMenu(tr("Pano"));
    clipMenu->addAction(tr("Panodan yapıştır (sunucuya gönder)"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+V")), this,
                        &MainWindow::onPasteToRemote);
    clipMenu->addAction(tr("Sunucu panosunu panoya kopyala"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+C")), this,
                        &MainWindow::onCopyFromRemote);
    clipMenu->addSeparator();
    QAction* clipHint = clipMenu->addAction(
        tr("Pano eşitlemesi çift yönlü ve otomatiktir; kopyalanan metin değiştiğinde "
           "diğer tarafa iletilir"));
    clipHint->setEnabled(false);

    QMenu* windowMenu = menuBar()->addMenu(tr("Pencere"));
    windowMenu->addAction(tr("Sonraki sekme"), QKeySequence(QStringLiteral("Ctrl+Tab")),
                          this, [this] {
        if (m_tabs->count() > 0)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    windowMenu->addAction(m_closeTabAction);

    statusBar()->showMessage(tr("Hazır"));
}

void MainWindow::loadProfiles()
{
    m_profiles = ConnectionStore::load();
}

void MainWindow::saveProfiles() const
{
    if (!ConnectionStore::save(m_profiles))
        qWarning() << "Baglanti listesi kaydedilemedi";
}

void MainWindow::refreshList()
{
    // Yenilemeden once secim ve grup acik/kapali durumu hatirlanir.
    const QString selectedId = selectedProfileId();
    QMap<QString, bool> expansion;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_list->topLevelItem(i);
        expansion.insert(group->text(0), group->isExpanded());
    }

    m_list->clear();

    // Kayitlar gruplarina gore ayrilir; grupsuz kayitlar "Genel" altina duser.
    QMap<QString, QList<ConnectionProfile>> byGroup;
    for (const ConnectionProfile& profile : m_profiles) {
        const QString key = profile.groupKey().isEmpty() ? tr("Genel") : profile.groupKey();
        byGroup[key].append(profile);
    }

    // "Genel" grubu hicbir kayit tasmayansa bile gizlenmez.
    const QString defaultGroup = tr("Genel");
    if (!byGroup.contains(defaultGroup))
        byGroup.insert(defaultGroup, QList<ConnectionProfile>());

    QStringList groupNames = byGroup.keys();
    std::sort(groupNames.begin(), groupNames.end(), [](const QString& a, const QString& b) {
        return QString::localeAwareCompare(a, b) < 0;
    });

    for (const QString& groupName : groupNames) {
        QList<ConnectionProfile> members = byGroup.value(groupName);
        std::sort(members.begin(), members.end(),
                  [](const ConnectionProfile& a, const ConnectionProfile& b) {
            return QString::localeAwareCompare(a.displayName(), b.displayName()) < 0;
        });

        auto* groupItem = new QTreeWidgetItem(m_list, QStringList(groupName));
        // Grup dugumu secilebilir; uzerinde baglanti islemi yoktur.
        groupItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        groupItem->setToolTip(0, tr("%1 sunucu").arg(members.size()));

        for (const ConnectionProfile& profile : members) {
            auto* item = new QTreeWidgetItem(groupItem, QStringList(profile.displayName()));
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            item->setData(0, Qt::UserRole, profile.id);
            item->setToolTip(0, profile.address()
                                   + (profile.viewOnly ? tr(" (yalnızca görüntüleme)")
                                                       : QString()));
        }

        // Gruplar acik baslar; kullanici kapatmissa durumu geri yuklenir.
        groupItem->setExpanded(expansion.value(groupName, true));
    }

    if (!selectProfile(selectedId)) {
        // Secim yok ya da silinmisse ilk sunucu sectirilir.
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* group = m_list->topLevelItem(i);
            if (group->childCount() > 0) {
                group->setExpanded(true);
                m_list->setCurrentItem(group->child(0));
                break;
            }
        }
    }
}

QTreeWidgetItem* MainWindow::findProfileItem(const QString& id) const
{
    if (id.isEmpty())
        return nullptr;

    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_list->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem* item = group->child(j);
            if (item->data(0, Qt::UserRole).toString() == id)
                return item;
        }
    }
    return nullptr;
}

QString MainWindow::selectedProfileId() const
{
    QTreeWidgetItem* item = m_list->currentItem();
    // Grup dugumleri kok seviyededir; onlar baglanti islemine girmez.
    if (!item || !item->parent())
        return {};
    return item->data(0, Qt::UserRole).toString();
}

bool MainWindow::selectProfile(const QString& id)
{
    QTreeWidgetItem* item = findProfileItem(id);
    if (!item)
        return false;

    if (QTreeWidgetItem* group = item->parent())
        group->setExpanded(true);
    m_list->setCurrentItem(item);
    return true;
}

ConnectionProfile MainWindow::profileById(const QString& id) const
{
    for (const ConnectionProfile& profile : m_profiles) {
        if (profile.id == id)
            return profile;
    }
    return {};
}

void MainWindow::updateProfileInList(const ConnectionProfile& profile)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == profile.id) {
            m_profiles[i] = profile;
            break;
        }
    }
    saveProfiles();

    // Ad ya da grup degistiyse dugum baska bir altina tasinir; bu yuzden
    // liste tam yenilenir (secim refreshList icinde korunur).
    refreshList();
}

void MainWindow::onNewConnection()
{
    ConnectionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ConnectionProfile profile = dialog.profile();
    m_profiles.append(profile);
    saveProfiles();
    refreshList();

    connectProfile(profile);
}

void MainWindow::onEditConnection()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        QMessageBox::information(this, tr("Düzenle"), tr("Önce listeden bir bağlantı seçin."));
        return;
    }

    const ConnectionProfile profile = profileById(id);
    if (profile.id.isEmpty())
        return;

    ConnectionDialog dialog(this);
    dialog.setProfile(profile);
    if (dialog.exec() != QDialog::Accepted)
        return;

    updateProfileInList(dialog.profile());
}

void MainWindow::onDeleteConnection()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        // Grup dugumu seciliyse kullaniciya ne yapmasi gerektigi soylenir.
        if (m_list->currentItem())
            QMessageBox::information(this, tr("Bağlantıyı sil"),
                                     tr("Önce listeden bir sunucu seçin."));
        return;
    }

    const ConnectionProfile profile = profileById(id);
    const QString question = tr("“%1” bağlantısı silinsin mi?").arg(profile.displayName());
    if (QMessageBox::question(this, tr("Bağlantıyı sil"), question,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == profile.id) {
            m_profiles.removeAt(i);
            break;
        }
    }
    saveProfiles();
    refreshList();
}

void MainWindow::onListContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_list->itemAt(pos);
    if (!item) {
        // Bos alanda sag tik: yeni baglanti
        QMenu menu(this);
        menu.addAction(tr("Yeni bağlantı..."), this, &MainWindow::onNewConnection);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        return;
    }

    m_list->setCurrentItem(item);

    QMenu menu(this);
    if (!item->parent()) {
        // Grup dugumu: baglanti islemi yok, yalnizca yeni kayit ve ac/kapa
        menu.addAction(tr("Yeni bağlantı..."), this, &MainWindow::onNewConnection);
        menu.addSeparator();
        menu.addAction(tr("Tümünü aç"), this, [item] { item->setExpanded(true); });
        menu.addAction(tr("Tümünü kapat"), this, [item] { item->setExpanded(false); });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        return;
    }

    menu.addAction(tr("Bağlan"), this, &MainWindow::onConnectCurrent);
    menu.addSeparator();
    menu.addAction(tr("Yeniden adlandır (F2)"), QKeySequence(Qt::Key_F2),
                   this, &MainWindow::onRenameConnection);
    menu.addAction(tr("Düzenle..."), QKeySequence(QStringLiteral("Ctrl+E")),
                   this, &MainWindow::onEditConnection);
    menu.addAction(tr("Sil"), this, &MainWindow::onDeleteConnection);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void MainWindow::onRenameConnection()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        if (m_list->currentItem())
            statusBar()->showMessage(tr("Yeniden adlandırılacak bir sunucu seçin"), 4000);
        return;
    }

    const ConnectionProfile profile = profileById(id);
    if (profile.id.isEmpty())
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Cihaza ad ver"),
        tr("“%1” için görünecek ad:").arg(profile.address()),
        QLineEdit::Normal, profile.name, &ok);
    if (!ok)
        return;

    ConnectionProfile updated = profile;
    updated.name = name.trimmed();
    updateProfileInList(updated);

    // Acik oturumlarin sekme basliklari da guncellensin.
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        VncView* view = viewForWidget(m_tabs->widget(tab));
        if (view && view->profileId() == updated.id)
            m_tabs->setTabText(tab, updated.displayName());
    }
}

void MainWindow::onExportConnections()
{
    saveProfiles();  // son hali diske yazilsin

    const QString destination = QFileDialog::getSaveFileName(
        this, tr("Bağlantı listesini dışa aktar"),
        QStringLiteral("xVNCManager-baglantilar.json"),
        tr("JSON dosyaları (*.json)"));
    if (destination.isEmpty())
        return;

    QFile::remove(destination);
    if (!QFile::copy(ConnectionStore::path(), destination)) {
        QMessageBox::warning(this, tr("Dışa aktarma"),
                             tr("Dosya kopyalanamadı:\n%1").arg(destination));
        return;
    }

    statusBar()->showMessage(tr("Bağlantı listesi kaydedildi: %1 — bu dosyayı ekiple paylaşabilirsiniz")
                                 .arg(destination), 8000);
}

void MainWindow::onImportConnections()
{
    const QString source = QFileDialog::getOpenFileName(
        this, tr("Bağlantı listesini içe aktar"), QString(),
        tr("JSON dosyaları (*.json)"));
    if (source.isEmpty())
        return;

    const QString target = ConnectionStore::path();
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(target).absoluteFilePath())
        return;

    // Once gecici kopya alinir; boylece kopya basarisiz olursa mevcut liste kaybolmaz.
    const QString temp = target + QStringLiteral(".importing");
    QFile::remove(temp);
    if (!QFile::copy(source, temp)) {
        QMessageBox::warning(this, tr("İçe aktarma"),
                             tr("Dosya okunamadı/kopyalanamadı:\n%1").arg(source));
        return;
    }

    QFile::remove(target);
    if (!QFile::rename(temp, target)) {
        QFile::remove(temp);
        QMessageBox::warning(this, tr("İçe aktarma"),
                             tr("Dosya yerine konulamadı:\n%1").arg(target));
        return;
    }

    loadProfiles();
    refreshList();
    statusBar()->showMessage(tr("Bağlantı listesi yüklendi: %1 (%2 kayıt)")
                                 .arg(source).arg(m_profiles.size()), 6000);
}

void MainWindow::onConnectCurrent()
{
    const QString id = selectedProfileId();
    if (id.isEmpty()) {
        statusBar()->showMessage(tr("Bağlanmak için listeden bir sunucu seçin"), 4000);
        return;
    }
    connectProfile(profileById(id));
}

void MainWindow::onProfileDoubleClicked()
{
    QTreeWidgetItem* item = m_list->currentItem();
    // Grup dugumunde cift tik baglamaz; acilir/kapanir.
    if (item && !item->parent()) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    onConnectCurrent();
}

void MainWindow::onQuickConnect()
{
    const QString text = m_quickHostEdit->text().trimmed();
    if (text.isEmpty())
        return;

    QString host = text;
    quint16 port = 5900;

    QString remainder = text;
    if (remainder.startsWith(QLatin1String("vnc://"), Qt::CaseInsensitive))
        remainder = remainder.mid(6);

    // "ad:port" veya vnc ayarindaki "ad::port" bicimi
    const int colon = remainder.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        const QString portText = remainder.mid(colon + 1);
        bool ok = false;
        const int value = portText.toInt(&ok);
        if (ok && value > 0 && value <= 65535) {
            port = static_cast<quint16>(value);
            host = remainder.left(colon);
            if (host.endsWith(QLatin1Char(':')))
                host.chop(1);
        }
    }
    if (host.isEmpty())
        return;

    ConnectionProfile profile = ConnectionProfile::createNew();
    profile.host = host;
    profile.port = port;
    profile.name = text;

    connectProfile(profile);
}

void MainWindow::connectProfile(ConnectionProfile profile)
{
    if (profile.host.isEmpty())
        return;

    // Ayrica kayitli bir profilde sifre yoksa sor
    if (profile.password.isEmpty()) {
        bool ok = false;
        const QString password = QInputDialog::getText(
            this, tr("Şifre gerekli"),
            tr("%1 sunucusunun VNC şifresi:").arg(profile.address()),
            QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
        profile.password = password;
    }

    // Daha acik bir sekme varsa oraya gec
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* view = qobject_cast<VncView*>(m_tabs->widget(i))) {
            if (view->profileId() == profile.id) {
                m_tabs->setCurrentIndex(i);
                return;
            }
        }
    }

    auto* view = new VncView(profile, this);
    QScrollArea* area = wrapView(view, profile.scaling);

    const int index = m_tabs->addTab(area, profile.displayName());
    m_tabs->setCurrentIndex(index);

    connect(view, &VncView::titleChanged, this, [this, area](const QString& title) {
        const int tab = m_tabs->indexOf(area);
        if (tab >= 0)
            m_tabs->setTabText(tab, title);
    });
    connect(view, &VncView::statusTextChanged, this, [this](const QString& text) {
        statusBar()->showMessage(text);
    });
    connect(view, &VncView::connectionClosed, this, [this, area](const QString& reason) {
        const int tab = m_tabs->indexOf(area);
        if (tab >= 0)
            m_tabs->setTabText(tab, m_tabs->tabText(tab) + tr(" [kopuk]"));
        statusBar()->showMessage(tr("Bağlantı kapandı: %1").arg(reason));
    });
    connect(view, &VncView::scalingChanged, this,
            [this, area, view](ScalingMode mode, qreal zoom) {
        area->setWidgetResizable(mode == ScalingMode::FitWindow);
        area->setAlignment(mode == ScalingMode::FitWindow
                               ? Qt::AlignLeft | Qt::AlignTop
                               : Qt::AlignCenter);

        // Etkin gorunun olcegi degistiyse eylemler/etiket guncellensin.
        if (area == m_tabs->currentWidget())
            updateScalingActions();

        ConnectionProfile profile = profileById(view->profileId());
        if (!profile.id.isEmpty()) {
            profile.scaling = mode;
            profile.zoomFactor = zoom;
            updateProfileInList(profile);
        }
    });
    connect(view, &VncView::zoomChanged, this, [this, area](qreal) {
        if (area == m_tabs->currentWidget())
            updateZoomLabel();
    });

    // Uzak imleci ve izleme/kontrol durumunu arac cubuguyla esle.
    QSettings settings;
    view->setRemoteCursorVisible(settings.value(QStringLiteral("view/remoteCursor"), true).toBool());
    connect(view, &VncView::viewOnlyChanged, this, [this](bool) {
        updateScalingActions();
    });
    connect(view, &VncView::sessionConnected, this, [this] {
        updateScalingActions();
    });
    connect(view, &VncView::connectionClosed, this, [this] {
        updateScalingActions();
    });

    statusBar()->showMessage(tr("Bağlanılıyor: %1").arg(profile.address()));
    updateScalingActions();

    // Kayit disindan gelen baglantilar (hizli baglan) basarili oldugunda
    // kayitli listeye eklenir ki sonradan ad verilebilsin.
    if (profileById(profile.id).id.isEmpty()) {
        connect(view, &VncView::sessionConnected, this, [this, profile] {
            addProfileToList(profile);
        });
    }
}

void MainWindow::addProfileToList(const ConnectionProfile& profile)
{
    // Ayni adresteki bir baglanti zaten kayitliysa tekrar eklenmez.
    for (const ConnectionProfile& existing : m_profiles) {
        if (existing.host.compare(profile.host, Qt::CaseInsensitive) == 0
            && existing.port == profile.port)
            return;
    }

    ConnectionProfile saved = profile;
    // Sifre DPAPI ile korunarak saklanir; istenmezse sonradan silinebilir.
    saved.savePassword = !saved.password.isEmpty();

    m_profiles.append(saved);
    saveProfiles();
    refreshList();
    selectProfile(saved.id);

    statusBar()->showMessage(tr("Listeye eklendi: %1 — ad vermek için F2")
                                 .arg(saved.displayName()), 6000);
}

QScrollArea* MainWindow::wrapView(VncView* view, ScalingMode mode) const
{
    auto* area = new QScrollArea;
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(view);
    area->setWidgetResizable(mode == ScalingMode::FitWindow);
    area->setAlignment(mode == ScalingMode::FitWindow
                           ? Qt::AlignLeft | Qt::AlignTop
                           : Qt::AlignCenter);
    area->setFocusPolicy(Qt::NoFocus);
    return area;
}

VncView* MainWindow::viewForWidget(QWidget* widget) const
{
    if (!widget)
        return nullptr;
    if (auto* view = qobject_cast<VncView*>(widget))
        return view;
    if (auto* area = qobject_cast<QScrollArea*>(widget))
        return qobject_cast<VncView*>(area->widget());
    return nullptr;
}

VncView* MainWindow::activeView() const
{
    return viewForWidget(m_tabs->currentWidget());
}

void MainWindow::applyScaling(VncView* view, ScalingMode mode)
{
    if (!view)
        return;

    // Kaydirma alani, profil kaydi ve eylemler scalingChanged sinyaliyle hallolur.
    view->setScalingMode(mode);
}

void MainWindow::onZoomIn()
{
    if (VncView* view = activeView())
        view->zoomStep(1);
}

void MainWindow::onZoomOut()
{
    if (VncView* view = activeView())
        view->zoomStep(-1);
}

void MainWindow::onDisconnectActive()
{
    if (!activeView()) {
        statusBar()->showMessage(tr("Kapatılacak bir oturum yok"), 3000);
        return;
    }
    onCloseActiveTab();
}

void MainWindow::onSendCtrlAltDelete()
{
    if (VncView* view = activeView()) {
        view->sendCtrlAltDelete();
        statusBar()->showMessage(tr("Ctrl+Alt+Del gönderildi"), 3000);
    }
}

void MainWindow::onSendWindowsKey()
{
    if (VncView* view = activeView()) {
        view->sendWindowsKey();
        statusBar()->showMessage(tr("Windows tuşu gönderildi"), 3000);
    }
}

void MainWindow::onViewOnlyToggled(bool checked)
{
    if (VncView* view = activeView())
        view->setViewOnly(!checked);
}

void MainWindow::onRemoteCursorToggled(bool checked)
{
    QSettings settings;
    settings.setValue(QStringLiteral("view/remoteCursor"), checked);

    if (VncView* view = activeView())
        view->setRemoteCursorVisible(checked);
}

void MainWindow::onShowConnectionInfo()
{
    VncView* view = activeView();
    if (!view) {
        statusBar()->showMessage(tr("Bilgi için önce bir oturum açın"), 4000);
        return;
    }

    ConnectionInfoDialog dialog(view->profile(), view, this);
    dialog.exec();
}

void MainWindow::onFileTransfer()
{
    VncView* view = activeView();
    if (!view || !view->session() || !view->isConnected()) {
        statusBar()->showMessage(tr("Dosya transferi için önce bağlanın"), 4000);
        return;
    }

    // Dosya transferi yetenekleri baglanti sirasinda sunucudan ogrenilir
    // (TightVNC guvenlik tipi 16 + yetenek listesi); sunucu FT kapaliysa
    // veya protokolu desteklemiyorsa hicbir mesaj gonderilmez.
    if (!view->session()->ftSupported()) {
        QMessageBox::information(
            this, tr("Dosya transferi"),
            tr("Bu sunucu dosya transferini desteklemiyor.\n\n"
               "Sunucuda dosya transferinin etkin olduğunu ve VNC şifresinin "
               "tam erişim şifresi olduğunu (izleme şifresi değil) kontrol edin."));
        return;
    }

    FileTransferDialog dialog(view->session(), view->profile().address(), this);
    dialog.exec();
}

void MainWindow::onPasteToRemote()
{
    VncView* view = activeView();
    if (!view) {
        statusBar()->showMessage(tr("Bağlı bir oturum yok"), 4000);
        return;
    }
    if (view->viewOnly()) {
        statusBar()->showMessage(tr("İzleme modunda pano gönderilemez — önce Kontrol moduna geçin"),
                                 5000);
        return;
    }
    if (view->sendLocalClipboardToRemote())
        statusBar()->showMessage(tr("Pano içeriği sunucuya gönderildi"), 4000);
    else
        statusBar()->showMessage(tr("Panoda gönderilecek metin yok"), 4000);
}

void MainWindow::onCopyFromRemote()
{
    VncView* view = activeView();
    if (!view) {
        statusBar()->showMessage(tr("Bağlı bir oturum yok"), 4000);
        return;
    }
    if (view->copyRemoteClipboardToLocal())
        statusBar()->showMessage(tr("Sunucu panosu panoya kopyalandı"), 4000);
    else
        statusBar()->showMessage(tr("Sunucudan henüz pano metni gelmedi"), 4000);
}

void MainWindow::onFitWindow()
{
    applyScaling(activeView(), ScalingMode::FitWindow);
}

void MainWindow::onActualSize()
{
    applyScaling(activeView(), ScalingMode::ActualSize);
}

void MainWindow::onToggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::onCloseActiveTab()
{
    const int index = m_tabs->currentIndex();
    if (index < 0)
        return;
    QWidget* widget = m_tabs->widget(index);
    m_tabs->removeTab(index);
    delete widget;
}

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index)
    updateScalingActions();

    if (auto* view = activeView()) {
        statusBar()->showMessage(view->title());
        view->setFocus();
    } else {
        statusBar()->showMessage(tr("Hazır"));
    }
}

void MainWindow::updateScalingActions()
{
    // buildUi sirasinda sekmeler eklenirken currentChanged sinyali tetiklenir;
    // o sira eylemler henuz olusturulmamis olabilir.
    if (!m_fitAction || !m_actualSizeAction || !m_fullScreenAction
        || !m_closeTabAction || !m_zoomInAction || !m_zoomOutAction
        || !m_viewOnlyAction || !m_cursorAction || !m_cadAction
        || !m_winAction || !m_disconnectAction || !m_infoAction
        || !m_transferAction)
        return;

    VncView* view = activeView();
    const bool connected = view != nullptr;
    const bool active = view && view->isConnected();

    m_fitAction->setEnabled(connected);
    m_actualSizeAction->setEnabled(connected);
    m_fullScreenAction->setEnabled(connected);
    m_closeTabAction->setEnabled(connected);
    m_disconnectAction->setEnabled(connected);
    m_infoAction->setEnabled(connected);
    m_transferAction->setEnabled(active);
    m_zoomInAction->setEnabled(connected);
    m_zoomOutAction->setEnabled(connected);
    m_cursorAction->setEnabled(connected);

    m_viewOnlyAction->setEnabled(active);
    m_cadAction->setEnabled(active && !view->viewOnly());
    m_winAction->setEnabled(active && !view->viewOnly());

    // Izleme/Kontrol dugumesi: isaretli = kontrol modu
    const bool controlMode = active && !view->viewOnly();
    m_viewOnlyAction->setChecked(controlMode);
    m_viewOnlyAction->setIcon(QIcon(controlMode ? QStringLiteral(":/icons/control.svg")
                                                : QStringLiteral(":/icons/viewonly.svg")));
    m_viewOnlyAction->setText(controlMode ? tr("Kontrol") : tr("İzleme"));

    if (view)
        m_cursorAction->setChecked(view->remoteCursorVisible());

    m_fitAction->setChecked(connected && view->scalingMode() == ScalingMode::FitWindow);
    m_actualSizeAction->setChecked(connected && view->scalingMode() == ScalingMode::ActualSize);

    updateZoomLabel();
}

void MainWindow::updateZoomLabel()
{
    if (!m_zoomLabel)
        return;

    const VncView* view = activeView();
    if (!view) {
        m_zoomLabel->setText(QStringLiteral("—"));
        m_zoomLabel->setToolTip(QString());
        return;
    }

    const qreal scale = view->effectiveScale();
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(scale * 100)));

    switch (view->scalingMode()) {
    case ScalingMode::FitWindow:
        m_zoomLabel->setToolTip(tr("Pencereye sığdır (otomatik ölçek)"));
        break;
    case ScalingMode::ActualSize:
        m_zoomLabel->setToolTip(tr("Tam görüntüleme, 1:1"));
        break;
    case ScalingMode::Zoom:
        m_zoomLabel->setToolTip(tr("Sabit yakınlaştırma: %1%").arg(qRound(scale * 100)));
        break;
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.sync();

    // Oturumlari temizce kapat
    while (m_tabs->count() > 0) {
        QWidget* widget = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete widget;
    }

    QMainWindow::closeEvent(event);
}
