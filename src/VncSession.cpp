#include "VncSession.h"

#include "FtProtocol.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>

#include <rfb/rfbclient.h>

// libvncclient'in statik VNC kimlik dogrulama yardimcisi (vncauth.c; sembol
// adi rfbclient.c icinde rfbEncryptBytes -> rfbClientEncryptBytes yapilir).
extern "C" void rfbClientEncryptBytes(unsigned char* bytes, char* passwd);

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

Q_LOGGING_CATEGORY(lcSession, "xvnc.session")

namespace {

// logCb statik oldugu icin hangi oturuma mesaj yazilacagini boyle biliriz.
thread_local VncSession* s_currentSession = nullptr;

// Ayristirma icin surekli dosya logu: uygulama klasorunde xvncmanager.log.
// libvncclient ciktilari, baglanti olaylari ve dosya transferi mesajlari
// buraya yazilir (sunucunun protokol tepkisini gormek icin).
std::mutex& logMutex()
{
    static std::mutex mutex;
    return mutex;
}

void appendLog(const QString& message)
{
    static QFile file;
    static bool opened = false;

    std::lock_guard<std::mutex> lock(logMutex());
    if (!opened) {
        file.setFileName(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/xvncmanager.log"));
        opened = file.open(QIODevice::Append | QIODevice::Text | QIODevice::WriteOnly);
        if (!opened)
            return;
    }

    const QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        + QLatin1Char(' ') + message + QLatin1Char('\n');
    file.write(line.toUtf8());
    file.flush();
}

// Dosya transferi icin cevap bekleme suresi (ms).
qint64 ftDeadline()
{
    return QDateTime::currentMSecsSinceEpoch() + 30000;
}

} // namespace
// ---------------------------------------------------------------------------
// TightVNC 2.x dosya transferi (0xFC alt protokolu) + Tight guvenlik tipi 16
//
// libvncclient'ta bu protokol icin hazir destek yoktur:
//  - Sunucu-->istemci mesajlari 0xFC (252) ile baslar; kutuphane bilinmeyen
//    tipte 256 bayt bosaltip olay dongusunu FALSE ile oldurur. Uzanti
//    handleMessage kancasi bu bayti iddia edip kalan 3 bayti okur.
//  - Guvenlik tipi 16 (Tight) yerlesik liste disindadir; uzanti
//    securityTypes ile sunulur ve yamali libvncclient
//    (prefer-extension-security-types.patch) bu tipleri tercih eder.
//    handleAuthentication Tight el sIKismasini yapar; SecurityResult'yi
//    kutuphane okur.
//
// Genisletme global bagli listeye (LIFO) bir kez eklenir ve rfbInitClient'dan
// once kayitli olmalidir.
// ---------------------------------------------------------------------------
rfbBool ftMessageCallback(rfbClient* client, rfbServerToClientMsg* message);
rfbBool tightAuthCallback(rfbClient* client, uint32_t authScheme);

static const uint32_t g_tightSecurityTypes[] = { 16 /* rfbTight */, 0 };

rfbClientProtocolExtension g_ftExtension = {
    nullptr,                // encodings (pseudo-encoding yok)
    nullptr,                // handleEncoding
    ftMessageCallback,
    nullptr,                // next
    g_tightSecurityTypes,   // securityTypes
    tightAuthCallback,
};

// 0xFC alt protokolu: ilk bayt 0xFC zaten okunmustur; kalan 3 baytla
// UINT32 kimlik olusturulur (TightVNC FTMessage.h readme'si).
rfbBool ftMessageCallback(rfbClient* client, rfbServerToClientMsg* message)
{
    if (message->type != 0xFC)
        return FALSE;

    unsigned char rest[3];
    if (!ReadFromRFBServer(client, reinterpret_cast<char*>(rest), 3))
        return TRUE;   // FALSE donmek olay dongusunu oldurur; kopma sonrasi adimda belli olur

    const quint32 messageId = (quint32(0xFC) << 24) | (quint32(rest[0]) << 16)
                              | (quint32(rest[1]) << 8) | quint32(rest[2]);

    appendLog(QStringLiteral("FT alindi: %1 (0x%2)")
                  .arg(QString::fromLatin1(Ft::messageIdName(messageId)))
                  .arg(messageId, 8, 16, QLatin1Char('0')));

    auto* session = static_cast<VncSession*>(rfbClientGetClientData(client, nullptr));
    if (session)
        session->handleTightFtMessage(client, messageId);
    return TRUE;
}

// Tight (16) guvenlik tipi handshake'i; tum cok baytli alanlar buyuk
// sonlandirilidir. Sira (TightVNC 2.8.63, rfb-sconn/RfbInitializer.cpp):
//   1) sunucu tunneling yetenek sayisini (u32) gonderir; 0 ise istemci
//      yanit yazmaz (TightVNC her zaman 0 gonderir).
//   2) sunucu kimlik dogrulama yeteneklerini gonderir (u32 sayi + 16'ar
//      bayt); istemci sectigi kodu u32 olarak geri yazar.
//   3) VNC kimlik dogrulamasi: 16 bayt challenge -> 16 bayt yanit
//      (TightVNC 2 doAuthVnc 16 bayt okur). SecurityResult'i libvncclient
//      okur (rfbInitClient -> default dal -> rfbHandleAuthResult).
rfbBool tightAuthCallback(rfbClient* client, uint32_t authScheme)
{
    Q_UNUSED(authScheme)

    quint8 buf4[4];
    if (!ReadFromRFBServer(client, reinterpret_cast<char*>(buf4), 4)) {
        appendLog(QStringLiteral("Tight handshake: tunneling sayisi okunamadi"));
        return FALSE;
    }
    const quint32 tunnelCount = Ft::getBe32(reinterpret_cast<char*>(buf4));
    if (tunnelCount > 0) {
        QByteArray skip(int(tunnelCount * 16u), '\0');
        if (!ReadFromRFBServer(client, skip.data(), quint32(skip.size()))) {
            appendLog(QStringLiteral("Tight handshake: tunneling yetenekleri okunamadi"));
            return FALSE;
        }
        quint8 zero[4] = { 0, 0, 0, 0 };   // tunnelling kullanilmiyor
        if (!WriteToRFBServer(client, reinterpret_cast<char*>(zero), 4))
            return FALSE;
    }

    if (!ReadFromRFBServer(client, reinterpret_cast<char*>(buf4), 4)) {
        appendLog(QStringLiteral("Tight handshake: kimlik dogrulama sayisi okunamadi"));
        return FALSE;
    }
    const quint32 authCount = Ft::getBe32(reinterpret_cast<char*>(buf4));

    quint32 chosen = 0;
    for (quint32 i = 0; i < authCount; ++i) {
        quint8 cap[16];
        if (!ReadFromRFBServer(client, reinterpret_cast<char*>(cap), 16)) {
            appendLog(QStringLiteral("Tight handshake: yetenekler okunamadi"));
            return FALSE;
        }
        const quint32 code = Ft::getBe32(reinterpret_cast<char*>(cap));
        if (chosen == 0 && code == 2 /* rfbAuthVNC */)
            chosen = code;
    }

    if (authCount == 0) {
        // Kimlik dogrulama gerekmiyor; SecurityResult bekle (kutuphane okur).
        appendLog(QStringLiteral("Tight handshake: kimlik dogrulama gerekmiyor"));
        return TRUE;
    }
    if (chosen == 0) {
        appendLog(QStringLiteral("Tight handshake: sunucu VNC kimlik dogrulamasi sunmuyor"));
        return FALSE;
    }

    quint8 chosenBytes[4];
    qToBigEndian(chosen, chosenBytes);
    if (!WriteToRFBServer(client, reinterpret_cast<char*>(chosenBytes), 4))
        return FALSE;

    quint8 challenge[16];
    if (!ReadFromRFBServer(client, reinterpret_cast<char*>(challenge), 16)) {
        appendLog(QStringLiteral("Tight handshake: challenge okunamadi"));
        return FALSE;
    }
    char* passwd = nullptr;
    if (client->GetPassword)
        passwd = client->GetPassword(client);
    if (!passwd || strlen(passwd) == 0) {
        appendLog(QStringLiteral("Tight handshake: sifre okunamadi"));
        return FALSE;
    }
    if (strlen(passwd) > 8)
        passwd[8] = '\0';
    rfbClientEncryptBytes(challenge, passwd);
    for (int i = int(strlen(passwd)); i >= 0; i--)
        passwd[i] = '\0';
    free(passwd);

    if (!WriteToRFBServer(client, reinterpret_cast<char*>(challenge), 16))
        return FALSE;

    appendLog(QStringLiteral("Tight handshake: VNC kimlik dogrulamasi gonderildi"));
    return TRUE;
}

void ensureFtExtensionRegistered()
{
    static std::once_flag once;
    std::call_once(once, [] { rfbClientRegisterExtension(&g_ftExtension); });
}

VncSession::VncSession(const ConnectionProfile& profile, QObject* parent)
    : QObject(parent)
    , m_profile(profile)
    , m_passwordUtf8(profile.password.toUtf8())
{
    rfbClientLog = &VncSession::logCb;
}

VncSession::~VncSession()
{
    // Normal yolda nesne yalnizca parcacik bittikten sonra silinir;
    // buraya join edilmemis bir parcacikla girmek programin sonlanmasi demek.
    m_running.store(false);
    if (m_thread.joinable())
        m_thread.detach();
}

void VncSession::start()
{
    if (m_running.exchange(true))
        return;

    m_disconnectReason.clear();
    m_thread = std::thread(&VncSession::run, this);
}

void VncSession::stop()
{
    // Bilerek join yapilmaz: GUI parcacigi asla arka plan parcaciginin
    // connect/DNS/okuma gibi bloklayici asamalarinda beklememelidir.
    m_running.store(false);
}

void VncSession::abandon()
{
    m_running.store(false);
    m_detached = true;

    if (!m_thread.joinable()) {
        // Parcacik hic baslamadi ya da zaten bitip finishOnGuiThread katildi.
        deleteLater();
    }
    // Katilabilirse finishOnGuiThread() katilip nesneyi silecek.
}

void VncSession::finishOnGuiThread()
{
    if (m_thread.joinable())
        m_thread.join();   // parcacik bu cagriyi gonderdikten hemen sonra biter
    m_threadFinished = true;

    if (m_detached)
        deleteLater();
}

void VncSession::sendPointer(int x, int y, quint8 mask)
{
    InputEvent event;
    event.type = InputType::Pointer;
    event.x = x;
    event.y = y;
    event.mask = mask;
    queueEvent(std::move(event));
}

void VncSession::sendKey(quint32 keysym, bool down)
{
    if (keysym == 0)
        return;

    InputEvent event;
    event.type = InputType::Key;
    event.keysym = keysym;
    event.down = down;
    queueEvent(std::move(event));
}

void VncSession::sendClipboard(const QString& utf8Text)
{
    if (utf8Text.isEmpty())
        return;

    InputEvent event;
    event.type = InputType::CutText;
    event.text = utf8Text;
    queueEvent(std::move(event));
}

void VncSession::queueEvent(InputEvent event)
{
    std::lock_guard<std::mutex> lock(m_inputMutex);
    m_inputQueue.push_back(std::move(event));
}

void VncSession::drainInput(rfbClient* client)
{
    std::deque<InputEvent> pending;
    {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        pending.swap(m_inputQueue);
    }

    for (const InputEvent& event : pending) {
        switch (event.type) {
        case InputType::Pointer:
            SendPointerEvent(client, event.x, event.y, event.mask);
            break;
        case InputType::Key:
            SendKeyEvent(client, event.keysym, event.down ? TRUE : FALSE);
            break;
        case InputType::CutText: {
            QByteArray utf8 = event.text.toUtf8();
            SendClientCutTextUTF8(client, utf8.data(), utf8.size());
            break;
        }
        }
    }
}

void VncSession::run()
{
    runBody();

    // Parcacik artik bitti: isaretin kalmasi, sonraki dosya transferi
    // isteklerinin hicbir zaman islenmemek uzere kuyrukta kalmasina yol acar.
    m_running.store(false);

    // Katilma ve (varsa) imha GUI parcacigina devredilir.
    if (QCoreApplication::instance())
        QMetaObject::invokeMethod(this, &VncSession::finishOnGuiThread,
                                  Qt::QueuedConnection);
}

void VncSession::runBody()
{
    s_currentSession = this;

    rfbClient* client = rfbGetClient(8, 3, 4);
    if (!client) {
        emit logMessage(tr("RFB istemcisi olusturulamadi"));
        emit disconnected(tr("RFB istemcisi olusturulamadi"));
        s_currentSession = nullptr;
        return;
    }

    // Varsayilan 60 sn'lik connect asimi arayuzu uzun sure kilitler;
    // erisilemeyen/adresi yanlis sunucular icin kisa bir esik kullanilir.
    client->connectTimeout = 8;   // saniye

    // Qt QImage::Format_RGB32 ile ayni bellek duzeni: B,G,R,X
    client->format.depth = 24;
    client->format.bitsPerPixel = 32;
    client->format.bigEndian = FALSE;
    client->format.trueColour = TRUE;
    client->format.redMax = client->format.greenMax = client->format.blueMax = 255;
    client->format.redShift = 16;
    client->format.greenShift = 8;
    client->format.blueShift = 0;

    // TightVNC sunucusunun destekledigi encoding'ler (tercih sirasina gore).
    client->appData.encodingsString = "copyrect tight zrle hextile corre rre raw";
    client->appData.compressLevel = 2;
    client->appData.qualityLevel = 6;
    client->appData.shareDesktop = m_profile.sharedSession ? TRUE : FALSE;
    client->appData.useRemoteCursor = FALSE;

    client->MallocFrameBuffer = &VncSession::mallocFrameBufferCb;
    client->GotFrameBufferUpdate = &VncSession::gotFrameBufferUpdateCb;
    client->GotXCutText = &VncSession::gotXCutTextCb;
    client->Bell = &VncSession::bellCb;
    client->GetPassword = &VncSession::getPasswordCb;

    rfbClientSetClientData(client, nullptr, this);

    // Tip-7 dosya transferi genisletmesi rfbInitClient'dan once kayitli olmali.
    ensureFtExtensionRegistered();

    std::free(client->serverHost);
    client->serverHost = strdup(m_profile.host.toUtf8().constData());
    client->serverPort = m_profile.port;
    m_client = client;

    emit logMessage(tr("Bağlanılıyor: %1").arg(m_profile.address()));
    appendLog(QStringLiteral("Bağlanılıyor: %1:%2").arg(m_profile.host).arg(m_profile.port));

    if (!rfbInitClient(client, nullptr, nullptr)) {
        // rfbInitClient basarisizlikta istemciyi zaten temizliyor.
        m_client = nullptr;
        std::free(m_frameBuffer);
        m_frameBuffer = nullptr;
        m_fbWidth = m_fbHeight = 0;
        const QString reason = m_disconnectReason.isEmpty()
                                   ? tr("Sunucuya bağlanılamadı: %1").arg(m_profile.address())
                                   : m_disconnectReason;
        appendLog(QStringLiteral("Bağlanılamadı: %1").arg(reason));
        emit disconnected(reason);
        s_currentSession = nullptr;
        return;
    }

    appendLog(QStringLiteral("Bağlandı (authScheme=%1)").arg(client->authScheme));

    // TightVNC el sIKismasinda (guvenlik tipi 16) sunucu, ServerInit'i takiben
    // etkilesim yetenekleri bloğunu gonderir; okunmazsa akis bozulur.
    if (client->authScheme == 16 /* rfbTight */) {
        if (!readTightInteractionCaps(client)) {
            rfbClientCleanup(client);
            m_client = nullptr;
            std::free(m_frameBuffer);
            m_frameBuffer = nullptr;
            m_fbWidth = m_fbHeight = 0;
            emit disconnected(tr("Sunucu yetenek listesi okunamadı"));
            s_currentSession = nullptr;
            return;
        }
    }

    m_desktopName = QString::fromUtf8(client->desktopName ? client->desktopName : "");
    emit connected(m_desktopName, client->width, client->height);
    emit logMessage(tr("Bağlandı: %1 (%2 x %3)")
                        .arg(m_desktopName)
                        .arg(client->width)
                        .arg(client->height));

    bool userStopped = false;
    while (true) {
        if (!m_running.load()) {
            userStopped = true;
            break;
        }

        drainInput(client);
        drainFtCommands(client);

        // 5 ms zaman asimi: bekleme esigini kisa tutup girdi gecikmesini dusurur.
        const int result = WaitForMessage(client, 5000);
        if (result < 0) {
            m_disconnectReason = tr("Sunucu bağlantısı kesildi");
            break;
        }
        if (result > 0 && !HandleRFBServerMessage(client)) {
            if (m_disconnectReason.isEmpty())
                m_disconnectReason = tr("Sunucu mesaji işlenemedi");
            break;
        }
    }

    // Yarim kalmis bir dosya islemi varsa kapat (GUI'den gelen sinyallerle).
    if (m_ftState.load() != FtState::Idle) {
        closeFtFile(true);
        m_ftState.store(FtState::Idle);
        emit ftFinished(false, tr("Bağlantı kesildiği için dosya işlemi sonlandırıldı"));
    }

    rfbClientCleanup(client);
    m_client = nullptr;
    std::free(m_frameBuffer);
    m_frameBuffer = nullptr;
    m_fbWidth = m_fbHeight = 0;

    appendLog(QStringLiteral("Dongu sonu: %1")
                  .arg(userStopped ? QStringLiteral("kullanici kapatti") : m_disconnectReason));
    emit disconnected(userStopped ? tr("Bağlantı kapatıldı") : m_disconnectReason);
    s_currentSession = nullptr;
}

bool VncSession::allocateFrameBuffer(rfbClient* client)
{
    const int width = client->width;
    const int height = client->height;
    if (width <= 0 || height <= 0)
        return FALSE;

    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    unsigned char* buffer = static_cast<unsigned char*>(std::malloc(bytes));
    if (!buffer)
        return FALSE;
    std::memset(buffer, 0, bytes);

    std::free(m_frameBuffer);
    m_frameBuffer = buffer;
    client->frameBuffer = buffer;
    m_fbWidth = width;
    m_fbHeight = height;

    emit desktopResized(width, height);
    return TRUE;
}

rfbBool VncSession::mallocFrameBufferCb(rfbClient* client)
{
    auto* self = static_cast<VncSession*>(rfbClientGetClientData(client, nullptr));
    return self ? self->allocateFrameBuffer(client) : FALSE;
}

void VncSession::gotFrameBufferUpdateCb(rfbClient* client, int x, int y, int w, int h)
{
    auto* self = static_cast<VncSession*>(rfbClientGetClientData(client, nullptr));
    if (!self || !client->frameBuffer || !client->width || !client->height)
        return;

    // Sunucu gonderdigi rect'i sinir disinda bildirebilir; guvenli hale getir.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > client->width)  w = client->width - x;
    if (y + h > client->height) h = client->height - y;
    if (w <= 0 || h <= 0)
        return;

    const QImage frame(client->frameBuffer, client->width, client->height,
                       client->width * 4, QImage::Format_RGB32);
    const QImage dirty = frame.copy(QRect(x, y, w, h));
    emit self->regionUpdated(dirty, x, y);
}

void VncSession::gotXCutTextCb(rfbClient* client, const char* text, int len)
{
    auto* self = static_cast<VncSession*>(rfbClientGetClientData(client, nullptr));
    if (!self || !text || len <= 0)
        return;

    emit self->serverCutText(QString::fromUtf8(text, len));
}

void VncSession::bellCb(rfbClient* client)
{
    auto* self = static_cast<VncSession*>(rfbClientGetClientData(client, nullptr));
    if (self)
        emit self->bell();
}

char* VncSession::getPasswordCb(rfbClient* client)
{
    auto* self = static_cast<VncSession*>(rfbClientGetClientData(client, nullptr));
    if (!self || self->m_passwordUtf8.isEmpty())
        return nullptr;

    // Kutuphane donen dizesi bellekten temizler; strdup ile uretilmelidir.
    return strdup(self->m_passwordUtf8.constData());
}

void VncSession::logCb(const char* format, ...)
{
    if (!format)
        return;

    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    const QString message = QString::fromUtf8(buffer).trimmed();
    if (message.isEmpty())
        return;

    appendLog(QStringLiteral("libvnc: %1").arg(message));

    // thread_local deger yalnizca bu (worker) parcaciginda gecerlidir;
    // kuyrukli cagri GUI parcaciginda calisacagi icin isareti onceden aliriz.
    VncSession* session = s_currentSession;

    if (session) {
        // Nesne GUI parcaciginda; baglam (context) nesnesi oldugu icin oturum
        // silinirse kuyrukli cagri otomatik olarak atlanir.
        QMetaObject::invokeMethod(session, [session, message] {
            emit session->logMessage(message);
        }, Qt::QueuedConnection);
    } else {
        qCInfo(lcSession) << message;
    }
}

// ---------------------------------------------------------------------------
// Dosya transferi istemcisi (TightVNC 2.x / 0xFC alt protokolu)
// ---------------------------------------------------------------------------
// docs/filetransfer-protocol.md'ye bakiniz. Iste istemciden sunucuya dogru
// gonderilir; cevaplar handleMessage kancasindan (handleTightFtMessage)
// gelir. Sunucu tek islem yurutur; devam eden is sirasindaki istekler
// geri cevrilir. Her istekte 30 sn'lik cevap suresi tazelenir.
// ---------------------------------------------------------------------------

void VncSession::ftListDrives()
{
    queueFtCmd({FtCmdType::Drives, {}, {}});
}

void VncSession::ftListDirectory(const QString& path)
{
    queueFtCmd({FtCmdType::List, path, {}});
}

void VncSession::ftDownload(const QString& remotePath, const QString& localPath)
{
    queueFtCmd({FtCmdType::Download, remotePath, localPath});
}

void VncSession::ftUpload(const QString& localPath, const QString& remotePath)
{
    queueFtCmd({FtCmdType::Upload, localPath, remotePath});
}

void VncSession::ftAbort()
{
    queueFtCmd({FtCmdType::Abort, {}, {}});
}

void VncSession::ftMkdir(const QString& path)
{
    queueFtCmd({FtCmdType::Mkdir, path, {}});
}

void VncSession::ftRemove(const QString& path)
{
    queueFtCmd({FtCmdType::Remove, path, {}});
}

void VncSession::ftRename(const QString& oldPath, const QString& newPath)
{
    queueFtCmd({FtCmdType::Rename, oldPath, newPath});
}

void VncSession::queueFtCmd(FtCmd cmd)
{
    if (!m_running.load()) {
        // Oturum yok: hemen sonuc don; GUI parcacigindan cagrilir.
        emit ftFinished(false, tr("Oturum açık değil"));
        return;
    }

    std::lock_guard<std::mutex> lock(m_ftCmdMutex);
    m_ftCmdQueue.push_back(std::move(cmd));
}

void VncSession::drainFtCommands(rfbClient* client)
{
    // Asim korumasi: sunucu hic cevap vermezse pencere takilmasin.
    if (m_ftState.load() != FtState::Idle && m_ftDeadlineMs != 0
        && QDateTime::currentMSecsSinceEpoch() > m_ftDeadlineMs) {
        closeFtFile(true);
        finishFt(false, tr("Sunucu dosya işlemine yanıt vermedi"));
    }

    std::deque<FtCmd> pending;
    {
        std::lock_guard<std::mutex> lock(m_ftCmdMutex);
        pending.swap(m_ftCmdQueue);
    }

    for (FtCmd& cmd : pending) {
        // Sunucu tek islem yurutur; devam eden is sirasindaki istekler
        // geri cevrilir (Abort her zaman kabul edilir).
        if (cmd.type != FtCmdType::Abort && m_ftState.load() != FtState::Idle) {
            emit ftFinished(false, tr("Başka bir dosya işlemi sürüyor"));
            continue;
        }

        switch (cmd.type) {
        case FtCmdType::Drives:
            // "kok" listesi suruculeri verir ("/" -> C:, D: ... klasor olarak).
            m_ftState.store(FtState::Drives);
            m_ftDeadlineMs = ftDeadline();
            appendLog(QStringLiteral("FT istek: surucu listesi"));
            writeFt(client, Ft::fileListRequest(QStringLiteral("/")));
            break;

        case FtCmdType::List:
            m_ftPath = cmd.a;
            m_ftEntries.clear();
            m_ftState.store(FtState::Listing);
            m_ftDeadlineMs = ftDeadline();
            appendLog(QStringLiteral("FT istek: listele %1").arg(cmd.a));
            writeFt(client, Ft::fileListRequest(Ft::toWirePath(cmd.a)));
            break;

        case FtCmdType::Download:
            m_ftPath = cmd.a;
            m_ftLocalPath = cmd.b;
            m_ftDone = 0;
            m_ftTotal = 0;
            m_ftDownloading = true;
            m_ftState.store(FtState::DlWaitDirSize);
            m_ftDeadlineMs = ftDeadline();
            // Ilerleme cubugu icin once toplam boyut istenir.
            appendLog(QStringLiteral("FT istek: indir %1 -> %2").arg(cmd.a, cmd.b));
            writeFt(client, Ft::dirSizeRequest(Ft::toWirePath(cmd.a)));
            emit ftProgress(0, 0);
            break;

        case FtCmdType::Upload: {
            auto* file = new QFile(cmd.a);
            if (!file->open(QIODevice::ReadOnly)) {
                delete file;
                emit ftFinished(false, tr("Yerel dosya açılamadı: %1").arg(cmd.a));
                break;
            }
            m_ftFile = file;
            m_ftLocalPath = cmd.a;
            m_ftPath = cmd.b;
            m_ftTotal = file->size();
            m_ftDone = 0;
            m_ftDownloading = false;
            m_ftUploadModMs =
                quint64(QFileInfo(cmd.a).lastModified().toMSecsSinceEpoch());
            m_ftState.store(FtState::UlWaitStart);
            m_ftDeadlineMs = ftDeadline();
            appendLog(QStringLiteral("FT istek: yükle %1 -> %2 (%3 bayt)")
                          .arg(cmd.a, cmd.b)
                          .arg(m_ftTotal));
            writeFt(client, Ft::uploadStartRequest(Ft::toWirePath(cmd.b)));
            emit ftProgress(0, m_ftTotal);
            break;
        }

        case FtCmdType::Abort:
            // Protokolde ayri bir iptal mesaji yoktur: aktarma yerel olarak
            // kesilir; sunucu tarafindaki akisi siradaki baslangic istegi kapatir.
            if (m_ftState.load() == FtState::Idle)
                break;
            closeFtFile(true);
            finishFt(false, tr("İşlem iptal edildi"));
            break;

        case FtCmdType::Mkdir:
            m_ftState.store(FtState::FsCommand);
            m_ftDeadlineMs = ftDeadline();
            appendLog(QStringLiteral("FT istek: klasör oluştur %1").arg(cmd.a));
            writeFt(client, Ft::mkdirRequest(Ft::toWirePath(cmd.a)));
            break;

        case FtCmdType::Remove:
            m_ftState.store(FtState::FsCommand);
            m_ftDeadlineMs = ftDeadline();
            appendLog(QStringLiteral("FT istek: sil %1").arg(cmd.a));
            writeFt(client, Ft::removeRequest(Ft::toWirePath(cmd.a)));
            break;

        case FtCmdType::Rename:
            m_ftState.store(FtState::FsCommand);
            m_ftDeadlineMs = ftDeadline();
            appendLog(QStringLiteral("FT istek: yeniden adlandır %1 -> %2").arg(cmd.a, cmd.b));
            writeFt(client, Ft::renameRequest(Ft::toWirePath(cmd.a), Ft::toWirePath(cmd.b)));
            break;
        }
    }
}

void VncSession::handleTightFtMessage(rfbClient* client, quint32 messageId)
{
    const FtState state = m_ftState.load();
    m_ftDeadlineMs = ftDeadline();   // her cevap asim sayacini tazeler

    switch (messageId) {
    case Ft::FileListReply: {
        QByteArray data;
        if (!readFtCompressedBlock(client, &data)) {
            if (state == FtState::Drives) {
                m_ftState.store(FtState::Idle);
                emit ftDrivesReceived({});
            } else if (state == FtState::Listing) {
                m_ftState.store(FtState::Idle);
                emit ftListingReceived(m_ftPath, {}, tr("Sunucu yanıtı okunamadı"));
            }
            break;
        }
        if (state == FtState::Drives) {
            // "kok" listesi suruculeri klasor bayragiyla verir ("C:" vb.).
            m_ftState.store(FtState::Idle);
            QStringList drives;
            QVector<Ft::Entry> entries;
            if (Ft::parseFileListEntries(data, &entries)) {
                for (const Ft::Entry& e : entries) {
                    if (e.isDir)
                        drives.append(e.name);
                }
            }
            emit ftDrivesReceived(drives);
            break;
        }
        if (state == FtState::Listing) {
            m_ftState.store(FtState::Idle);
            QVector<FtEntry> entries;
            QString error;
            if (!Ft::parseFileListEntries(data, &entries)) {
                entries.clear();
                error = tr("Sunucu yanıtı okunamadı");
            }
            const QString path = m_ftPath;
            emit ftListingReceived(path, entries, error);
        }
        break;
    }

    case Ft::DirSizeReply: {
        if (state != FtState::DlWaitDirSize)
            break;
        quint8 buf[8];
        if (!ReadFromRFBServer(client, reinterpret_cast<char*>(buf), 8))
            break;
        m_ftTotal = qint64(Ft::getBe64(reinterpret_cast<char*>(buf)));
        m_ftState.store(FtState::DlWaitStart);
        writeFt(client, Ft::downloadStartRequest(Ft::toWirePath(m_ftPath), 0));
        break;
    }

    case Ft::DownloadStartReply: {
        if (state != FtState::DlWaitStart)
            break;
        m_ftFile = new QFile(m_ftLocalPath);
        if (!m_ftFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            delete m_ftFile;
            m_ftFile = nullptr;
            m_ftDownloading = false;
            m_ftState.store(FtState::Idle);
            emit ftFinished(false, tr("Yerel dosya yazılamadı: %1").arg(m_ftLocalPath));
            break;
        }
        m_ftDone = 0;
        m_ftState.store(FtState::DlWaitData);
        emit ftProgress(0, m_ftTotal);
        requestFtDownloadChunk(client);
        break;
    }

    case Ft::DownloadDataReply: {
        if (state != FtState::DlWaitData || !m_ftFile)
            break;
        QByteArray data;
        if (!readFtCompressedBlock(client, &data)) {
            closeFtFile(true);
            finishFt(false, tr("Sunucu verisi beklenmedik biçimde sıkıştırılmış"));
            break;
        }
        if (m_ftFile->write(data) != data.size()) {
            closeFtFile(true);
            finishFt(false, tr("Yerel diske yazılamadı (disk dolu olabilir)"));
            break;
        }
        m_ftDone += data.size();
        emit ftProgress(m_ftDone, m_ftTotal);
        // Sunucu dosya sonuna gelindiginde bir sonraki istege DownloadEndReply
        // ile cevap verir; o zamana kadar parca istemeye devam edilir.
        requestFtDownloadChunk(client);
        break;
    }

    case Ft::DownloadEndReply: {
        if (state != FtState::DlWaitData)
            break;
        // u8 dosya bayraklari + u64 degisiklik zamani (ms): okunup atilir.
        unsigned char trailer[9];
        if (!ReadFromRFBServer(client, reinterpret_cast<char*>(trailer), 9))
            break;
        const qint64 total = m_ftTotal > 0 ? m_ftTotal : m_ftDone;
        closeFtFile(false);
        m_ftDownloading = false;
        emit ftProgress(total, total);
        finishFt(true, tr("İndirme tamamlandı: %1").arg(m_ftLocalPath));
        break;
    }

    case Ft::UploadStartReply: {
        if (state != FtState::UlWaitStart)
            break;
        // Ilk parca; sonraki parcalar her UploadDataReply'den sonra gonderilir.
        m_ftState.store(FtState::UlWaitData);
        emit ftProgress(m_ftDone, m_ftTotal);
        sendFtUploadChunk(client);
        break;
    }

    case Ft::UploadDataReply:
        if (state == FtState::UlWaitData)
            sendFtUploadChunk(client);
        break;

    case Ft::UploadEndReply:
        if (state == FtState::UlWaitEnd)
            finishFt(true, tr("Yükleme tamamlandı: %1").arg(m_ftPath));
        break;

    case Ft::MkdirReply:
        if (state == FtState::FsCommand)
            finishFt(true, tr("Klasör oluşturuldu"));
        break;

    case Ft::RemoveReply:
        if (state == FtState::FsCommand)
            finishFt(true, tr("Silindi"));
        break;

    case Ft::RenameReply:
        if (state == FtState::FsCommand)
            finishFt(true, tr("Yeniden adlandırıldı"));
        break;

    case Ft::CompressionSupportReply: {
        // Istemedigimiz bir destek cevabi; govde 1 bayt (okunup atilir).
        quint8 supported = 0;
        ReadFromRFBServer(client, reinterpret_cast<char*>(&supported), 1);
        break;
    }

    case Ft::LastRequestFailedReply: {
        // StringUTF8 hata mesaji; beklentiyi duruma gore karsilar.
        QString reason;
        quint8 lenBuf[4];
        if (ReadFromRFBServer(client, reinterpret_cast<char*>(lenBuf), 4)) {
            const quint32 len = Ft::getBe32(reinterpret_cast<char*>(lenBuf));
            if (len <= Ft::MaxErrorBytes) {
                QByteArray text(int(len), '\0');
                if (len == 0 || ReadFromRFBServer(client, text.data(), len)) {
                    while (!text.isEmpty() && text.endsWith('\0'))
                        text.chop(1);   // sunucu metinlerine NUL sonlandirici ekler
                    reason = QString::fromUtf8(text);
                }
            } else {
                // Asiri uzun metin: akisi temizlemek icin parca parca okunup atilir.
                QByteArray chunk(65536, '\0');
                for (quint32 remaining = len; remaining > 0;) {
                    const quint32 n = qMin<quint32>(remaining, quint32(chunk.size()));
                    if (!ReadFromRFBServer(client, chunk.data(), n))
                        break;
                    remaining -= n;
                }
            }
        }
        if (reason.isEmpty())
            reason = tr("Sunucu işlemi reddetti");
        appendLog(QStringLiteral("FT reddedildi: %1").arg(reason));

        if (state == FtState::Drives) {
            m_ftState.store(FtState::Idle);
            emit ftDrivesReceived({});
        } else if (state == FtState::Listing) {
            m_ftState.store(FtState::Idle);
            emit ftListingReceived(m_ftPath, {}, reason);
        } else {
            closeFtFile(true);
            finishFt(false, reason);
        }
        break;
    }

    default:
        // Bilinmeyen cevap kimligi: govde boyutu bilinmedigi icin akis
        // senkronizasyonu artik guvende degildir.
        appendLog(QStringLiteral("Bilinmeyen FT cevabi: 0x%1")
                      .arg(messageId, 8, 16, QLatin1Char('0')));
        if (state != FtState::Idle) {
            closeFtFile(true);
            finishFt(false, tr("Sunucudan beklenmedik dosya transferi yanıtı"));
        }
        break;
    }
}

// CompressedBlock okur (u8 seviye + u32 sikistirilmis boyut + u32 ham boyut +
// veri). Istedigimiz seviye 0'dir (ham); tutarsizlikta dahi veri okunur ki
// akis senkronizasyonu korunsun.
bool VncSession::readFtCompressedBlock(rfbClient* client, QByteArray* data)
{
    unsigned char header[9];
    if (!ReadFromRFBServer(client, reinterpret_cast<char*>(header), 9))
        return false;

    const quint8 level = header[0];
    const quint32 coSize = Ft::getBe32(reinterpret_cast<char*>(header) + 1);
    const quint32 uncoSize = Ft::getBe32(reinterpret_cast<char*>(header) + 5);

    QByteArray payload(int(coSize), '\0');
    if (coSize > 0 && !ReadFromRFBServer(client, payload.data(), coSize))
        return false;

    if (level != 0 || coSize != uncoSize || coSize > Ft::MaxBlockBytes)
        return false;

    *data = payload;
    return true;
}

void VncSession::requestFtDownloadChunk(rfbClient* client)
{
    writeFt(client, Ft::downloadDataRequest(Ft::ChunkSize));
}

// Yukleme: her cevaptan sonra bir parca gonderilir (sunucu her parca icin
// UploadDataReply ile onay verir - akis kontrolu); dosya bitince kapanis
// isteği (bayraklar + degisiklik zamani) gonderilir.
void VncSession::sendFtUploadChunk(rfbClient* client)
{
    if (!m_ftFile) {
        finishFt(false, tr("Yükleme dosyası kapandı"));
        return;
    }

    char buffer[Ft::ChunkSize];
    const qint64 n = m_ftFile->read(buffer, Ft::ChunkSize);
    if (n < 0) {
        closeFtFile(false);
        finishFt(false, tr("Yerel dosya okunamadı: %1").arg(m_ftLocalPath));
        return;
    }
    if (n == 0) {
        // Dosya bitti: kapanis istegi; sunucu UploadEndReply ile onaylar.
        m_ftState.store(FtState::UlWaitEnd);
        writeFt(client, Ft::uploadEndRequest(0, m_ftUploadModMs));
        return;
    }
    if (!writeFt(client, Ft::uploadDataRequest(QByteArray(buffer, int(n)))))
        return;   // yazma basarisiz: baglanti koptu, kopma dongude raporlanir

    m_ftDone += n;
    emit ftProgress(m_ftDone, m_ftTotal);
}

bool VncSession::writeFt(rfbClient* client, const QByteArray& message)
{
    if (!client)
        return false;

    if (!WriteToRFBServer(client, message.constData(), unsigned(message.size()))) {
        if (m_disconnectReason.isEmpty())
            m_disconnectReason = tr("Sunucuya veri yazılamadı");
        return false;
    }
    return true;
}

void VncSession::closeFtFile(bool deletePartial)
{
    if (!m_ftFile)
        return;

    m_ftFile->close();
    delete m_ftFile;
    m_ftFile = nullptr;

    // Yalnizca yarim kalmis indirme hedefi silinir; yukleme kaynagi asla.
    if (deletePartial && m_ftDownloading && !m_ftLocalPath.isEmpty())
        QFile::remove(m_ftLocalPath);
}

void VncSession::finishFt(bool ok, const QString& message)
{
    m_ftState.store(FtState::Idle);
    m_ftEntries.clear();
    m_ftDownloading = false;
    emit ftFinished(ok, message);
}

// TightVNC etkilesim yetenekleri: ServerInit'i takiben 8 bayt baslik
// (u16 sunucu mesajlari, u16 istemci mesajlari, u16 encoding, u16 pad) +
// her liste icin 16'ar baytlik kayitlar (u32 kod + 4 bayt vendor + 8 bayt
// imza). Dosya transferi yetenek kodlari (0xFC0001xx) burada duyurulur;
// sunucu FT kapaliysa bu kodlar listelenmez.
bool VncSession::readTightInteractionCaps(rfbClient* client)
{
    unsigned char capsHeader[8];
    if (!ReadFromRFBServer(client, reinterpret_cast<char*>(capsHeader), 8)) {
        appendLog(QStringLiteral("Yetenek listesi basligi okunamadi"));
        return false;
    }
    const int nSrv = Ft::getBe16(reinterpret_cast<char*>(capsHeader));
    const int nCl = Ft::getBe16(reinterpret_cast<char*>(capsHeader) + 2);
    const int nEnc = Ft::getBe16(reinterpret_cast<char*>(capsHeader) + 4);
    const int total = (nSrv + nCl + nEnc) * 16;

    QByteArray caps(total, '\0');
    if (total > 0 && !ReadFromRFBServer(client, caps.data(), unsigned(total))) {
        appendLog(QStringLiteral("Yetenek listesi okunamadi (srv=%1 cl=%2 enc=%3)")
                      .arg(nSrv).arg(nCl).arg(nEnc));
        return false;
    }

    for (int off = 0; off + 16 <= caps.size(); off += 16) {
        const quint32 code = Ft::getBe32(caps.constData() + off);
        if (code == Ft::FileListReply)
            m_ftSupported = true;
    }
    appendLog(QStringLiteral("Etkilesim yetenekleri: srv=%1 cl=%2 enc=%3 ft=%4")
                  .arg(nSrv).arg(nCl).arg(nEnc)
                  .arg(m_ftSupported ? QStringLiteral("var") : QStringLiteral("yok")));
    return true;
}
