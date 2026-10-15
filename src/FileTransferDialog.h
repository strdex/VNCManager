#pragma once

#include "VncSession.h"

#include <QDialog>
#include <QPointer>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// TightVNC'nin dosya transferi penceresi benzeri cift panelli duzen:
//   solda yerel bilgisayar (istemci), sagda uzak bilgisayar (sunucu),
//   ortada >> (yukle) / << (indir), altta ilerleme ve iptal.
// Uzak taraf rfbFileTransfer (tip-7) protokoluyla calisir; yerel taraf Qt ile.
class FileTransferDialog : public QDialog
{
    Q_OBJECT

public:
    FileTransferDialog(VncSession* session, const QString& serverTitle,
                       QWidget* parent = nullptr);

private slots:
    // Uzak pano
    void navigateRemote(const QString& path);
    void remoteUp();
    void refreshRemote();
    void onRemoteActivated(QTreeWidgetItem* item, int column);
    void onRemoteMenu(const QPoint& pos);

    // Yerel pano
    void navigateLocal(const QString& path);
    void localUp();
    void refreshLocal();
    void onLocalActivated(QTreeWidgetItem* item, int column);
    void onLocalMenu(const QPoint& pos);

    // Aktarma
    void uploadSelected();      // >>
    void downloadSelected();    // <<

    // Yerel islemler
    void renameLocal();
    void mkdirLocal();
    void removeLocal();

    // Uzak islemler
    void renameRemote();
    void mkdirRemote();
    void removeRemote();

    void cancelTransfer();

    // Oturum sinyalleri
    void onListing(const QString& path, const QVector<VncSession::FtEntry>& entries,
                   const QString& error);
    void onDrives(const QStringList& drives);
    void onProgress(qint64 done, qint64 total);
    void onFinish(bool ok, const QString& message);

private:
    enum class Side { Local, Remote };

    void setBusy(bool busy);
    void setStatus(const QString& text, bool error = false);
    void copyToClipboard(Side side, bool nameOnly);
    void showMenu(Side side, const QPoint& pos);

    QVector<VncSession::FtEntry> localEntries(const QString& path) const;
    void fillPane(Side side, const QVector<VncSession::FtEntry>& entries);

    QString selectedPath(Side side) const;
    QString selectedName(Side side) const;
    bool selectedIsDir(Side side) const;
    bool hasSelection(Side side) const;

    static QString joinPath(const QString& dir, const QString& name);
    static QString parentPath(const QString& path);
    static QString displayName(const QString& path);

    // Yaygin Windows hata kodlarini anlasilir Turkceye cevirir ("Error 145")
    static QString translateSystemError(const QString& text);
    // Basarisiz yerel islemden sonra sistem hata kodu ("Hata 145")
    static QString lastSystemErrorText();

    QPointer<VncSession> m_session;

    QString m_localPath = QStringLiteral("/");    // "/" = surucu listesi
    QString m_remotePath = QStringLiteral("/");   // "/" = surucu listesi
    QStringList m_drives;

    bool m_busy = false;
    bool m_refreshRemote = false;
    bool m_refreshLocal = false;

    // Yerel pano
    QLineEdit* m_localPathEdit = nullptr;
    QTreeWidget* m_localList = nullptr;
    QPushButton* m_localUp = nullptr;
    QPushButton* m_localRename = nullptr;
    QPushButton* m_localMkdir = nullptr;
    QPushButton* m_localRemove = nullptr;
    QPushButton* m_localRefresh = nullptr;

    // Uzak pano
    QLineEdit* m_remotePathEdit = nullptr;
    QTreeWidget* m_remoteList = nullptr;
    QPushButton* m_remoteUp = nullptr;
    QPushButton* m_remoteRename = nullptr;
    QPushButton* m_remoteMkdir = nullptr;
    QPushButton* m_remoteRemove = nullptr;
    QPushButton* m_remoteRefresh = nullptr;

    // Aktarma
    QPushButton* m_uploadButton = nullptr;    // >>
    QPushButton* m_downloadButton = nullptr;  // <<

    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_cancelButton = nullptr;
};
