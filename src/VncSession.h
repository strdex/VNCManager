#pragma once

#include "ConnectionProfile.h"
#include "FtProtocol.h"

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

struct _rfbClient;
typedef struct _rfbClient rfbClient;

class QFile;

// rfb/rfbproto.h ile birebir ayni tanim; header'i agir windows basliklari
// acmadan kullanabilmek icin burada tekrarlanir.
typedef int8_t rfbBool;

// libvncclient'i bir arka plan is parcaciginda calistiran sarmalayici.
// RFB dongusu Qt'nin olay dongusunden bagimsizdir; tum Qt cagrilari
// kuyrukli baglantilarla ana is parcacigina tasinir.
class VncSession : public QObject
{
    Q_OBJECT

public:
    explicit VncSession(const ConnectionProfile& profile, QObject* parent = nullptr);
    ~VncSession() override;

    void start();

    // Calisiyor olabilecek arka plan parcacigini durdurur; BLOKLAMAZ.
    // Parcacik en fazla connectTimeout/readTimeout kadar sonra sonlanir.
    void stop();

    // Sahibi (VncView) yok oluyor: durdur, imhayi parcacigin sonuna birak.
    // Nesne, parcacik bittiginde GUI is parcaciginda kendini siler.
    void abandon();

    bool isRunning() const { return m_running.load(); }
    int framebufferWidth() const { return m_fbWidth; }
    int framebufferHeight() const { return m_fbHeight; }
    QString desktopName() const { return m_desktopName; }

    // Girdi olaylari istemci parcacigina kuyruklanir; orada socket'e yazilir.
    void sendPointer(int x, int y, quint8 mask);
    void sendKey(quint32 keysym, bool down);
    void sendClipboard(const QString& utf8Text);

    // ---- Dosya transferi (rfbFileTransfer / tip-7) ------------------------------
    // Istekler istemci parcacigindaki kuyruga yazilir; tum yazma islemleri orada
    // yapilir. Yanitlar queued sinyallerle GUI'ye doner. Kayit kaydi Ft::Entry
    // ile ayni tiptir.
    using FtEntry = Ft::Entry;

    void ftListDrives();
    void ftListDirectory(const QString& path);
    void ftDownload(const QString& remotePath, const QString& localPath);
    void ftUpload(const QString& localPath, const QString& remotePath);
    void ftAbort();

    // Uzak dosya sistemi komutlari (TightVNC 2 alt protokolu): klasor olustur,
    // sil, yeniden adlandir.
    void ftMkdir(const QString& path);
    void ftRemove(const QString& path);
    void ftRename(const QString& oldPath, const QString& newPath);

    bool ftBusy() const { return m_ftState != FtState::Idle; }

    // Sunucu, dosya transferi yeteneklerini (0xFC kodlari) yetenek
    // listesinde duyurdu mu? Baglanti sirasinda doldurulur.
    bool ftSupported() const { return m_ftSupported; }

    // Yalnizca dahili: genisletme kancasindan cagrilir (VncSession.cpp).
    void handleTightFtMessage(rfbClient* client, quint32 messageId);

signals:
    void connected(const QString& desktopName, int width, int height);
    void disconnected(const QString& reason);
    void regionUpdated(const QImage& image, int x, int y);
    void desktopResized(int width, int height);
    void serverCutText(const QString& text);
    void bell();
    void logMessage(const QString& message);

    // Dosya transferi
    void ftDrivesReceived(const QStringList& drives);
    void ftListingReceived(const QString& path, const QVector<FtEntry>& entries,
                           const QString& error);
    void ftProgress(qint64 done, qint64 total);
    void ftFinished(bool ok, const QString& message);

private:
    enum class InputType { Pointer, Key, CutText };

    struct InputEvent {
        InputType type = InputType::Pointer;
        int x = 0;
        int y = 0;
        int mask = 0;
        quint32 keysym = 0;
        bool down = false;
        QString text;
    };

    void run();
    void runBody();
    void finishOnGuiThread();
    void drainInput(rfbClient* client);
    void queueEvent(InputEvent event);

    // ---- Dosya transferi (istemci parcacigi) ----------------------------------
    enum class FtState {
        Idle,
        Drives,          // surucu listesi ("/") bekleniyor
        Listing,         // dizin icerigi bekleniyor
        DlWaitDirSize,   // indirme: toplam boyut cevabi bekleniyor
        DlWaitStart,     // indirme: baslangic cevabi bekleniyor
        DlWaitData,      // indirme: veri/son cevabi bekleniyor
        UlWaitStart,     // yukleme: baslangic cevabi bekleniyor
        UlWaitData,      // yukleme: parca cevabi bekleniyor
        UlWaitEnd,       // yukleme: kapanis cevabi bekleniyor
        FsCommand,       // mkdir/sil/yeniden adlandir cevabi bekleniyor
    };
    enum class FtCmdType { Drives, List, Download, Upload, Abort, Mkdir, Remove, Rename };

    struct FtCmd {
        FtCmdType type = FtCmdType::List;
        QString a;
        QString b;
    };

    void queueFtCmd(FtCmd cmd);
    void drainFtCommands(rfbClient* client);
    bool writeFt(rfbClient* client, const QByteArray& message);
    bool readFtCompressedBlock(rfbClient* client, QByteArray* data);
    void requestFtDownloadChunk(rfbClient* client);
    void sendFtUploadChunk(rfbClient* client);
    void finishFt(bool ok, const QString& message);
    void closeFtFile(bool deletePartial);
    bool readTightInteractionCaps(rfbClient* client);

    bool allocateFrameBuffer(rfbClient* client);

    static rfbBool mallocFrameBufferCb(rfbClient* client);
    static void gotFrameBufferUpdateCb(rfbClient* client, int x, int y, int w, int h);
    static void gotXCutTextCb(rfbClient* client, const char* text, int len);
    static void bellCb(rfbClient* client);
    static char* getPasswordCb(rfbClient* client);
    static void logCb(const char* format, ...);

    ConnectionProfile m_profile;
    QByteArray m_passwordUtf8;

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    // Yalnizca GUI is parcaciginda yazilir/okunur.
    bool m_detached = false;
    bool m_threadFinished = false;

    std::mutex m_inputMutex;
    std::deque<InputEvent> m_inputQueue;

    rfbClient* m_client = nullptr;
    unsigned char* m_frameBuffer = nullptr;
    int m_fbWidth = 0;
    int m_fbHeight = 0;
    QString m_desktopName;
    QString m_disconnectReason;

    // Dosya transferi: kuyruk GUI'den doldurulur, diger her sey istemci parcaciginda.
    std::mutex m_ftCmdMutex;
    std::deque<FtCmd> m_ftCmdQueue;

    // Durum yalnizca istemci parcaciginca degistirilir; GUI ftBusy() ile okur.
    std::atomic<FtState> m_ftState{FtState::Idle};
    QString m_ftPath;                 // uzak dizin/dosya yolu (uygulama bicimi)
    QString m_ftLocalPath;            // yerel dosya yolu
    QFile* m_ftFile = nullptr;        // indirme hedefi / yukleme kaynagi
    qint64 m_ftDone = 0;
    qint64 m_ftTotal = 0;
    QVector<FtEntry> m_ftEntries;
    quint64 m_ftUploadModMs = 0;      // yukleme sonunda gonderilecek degisiklik zamani
    qint64 m_ftDeadlineMs = 0;        // sunucu cevapsiz kalirsa islemi kes (ms)
    bool m_ftDownloading = false;     // acik dosya indirme hedefi mi (silme korumasi)
    bool m_ftSupported = false;       // sunucu 0xFC dosya transferini duyurdu mu
};

Q_DECLARE_METATYPE(VncSession::FtEntry)
Q_DECLARE_METATYPE(QVector<VncSession::FtEntry>)
