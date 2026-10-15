#pragma once

// ---------------------------------------------------------------------------
// Dosya transferi: TightVNC 2.x alt protokolu (0xFC mesaj kimlikleri)
//
// Kaynak: TightVNC 2.8.63 GPL kaynak kodu (ft-common/FTMessage.h,
// ft-client-lib/FileTransferRequestSender.cpp + FileTransferReplyBuffer.cpp,
// ft-server-lib/FileTransferRequestHandler.cpp).
//
// Mesaj kimlikleri 4 bayttir ve 0xFC (252) ile baslar: istemci ilk bayti
// okur, 0xFC ise kalan 3 bayti okuyup UINT32 kimligi olusturur (sunucu da
// ayni sekilde yapar - FTMessage.h readme). Tum cok baytli alanlar buyuk
// sonlandirislidir (RFB).
//
// Yol bicimi unix tarzidir: "/C:/Users" gibi; surucu listesi icin "/"
// listelenir ve suruculer klasor bayragiyla doner.
//
// Sikistirma: isteklerde seviye 0 istenir; sunucu ham veri gonderir.
// CompressedBlock: u8 seviye, u32 sikistirilmis boyut, u32 ham boyut, veri.
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QtEndian>

namespace Ft {

// Mesaj kimlikleri (tek yonlu: istek veya cevap)
enum MessageId : quint32 {
    CompressionSupportRequest = 0xFC000100u,
    CompressionSupportReply   = 0xFC000101u,
    FileListRequest           = 0xFC000102u,
    FileListReply             = 0xFC000103u,
    UploadStartRequest        = 0xFC000106u,
    UploadStartReply          = 0xFC000107u,
    UploadDataRequest         = 0xFC000108u,
    UploadDataReply           = 0xFC000109u,
    UploadEndRequest          = 0xFC00010Au,
    UploadEndReply            = 0xFC00010Bu,
    DownloadStartRequest      = 0xFC00010Cu,
    DownloadStartReply        = 0xFC00010Du,
    DownloadDataRequest       = 0xFC00010Eu,
    DownloadDataReply         = 0xFC00010Fu,
    DownloadEndReply          = 0xFC000110u,
    MkdirRequest              = 0xFC000111u,
    MkdirReply                = 0xFC000112u,
    RemoveRequest             = 0xFC000113u,
    RemoveReply               = 0xFC000114u,
    RenameRequest             = 0xFC000115u,
    RenameReply               = 0xFC000116u,
    DirSizeRequest            = 0xFC000117u,
    DirSizeReply              = 0xFC000118u,
    LastRequestFailedReply    = 0xFC000119u,
};

// Dosya kayit / kapanis bayraklari
enum FileFlags : quint16 {
    FlagDirectory = 0x1,
    FlagExe       = 0x2,
};

struct Entry {
    QString name;          // yalnizca ad ("C:" veya "belge.txt"); yol degil
    quint64 size = 0;      // klasorlerde 0
    QDateTime modified;    // yerel zamanda; gecersizse bos
    bool isDir = false;
    bool isExe = false;
};

// Indirme/yukleme parca boyutu (TightVNC uyarli akis icin 20000..400000)
constexpr quint32 ChunkSize = 65536;

// Akisi bozmamak icin okuma tavanlari
constexpr quint32 MaxBlockBytes = 64u * 1024u * 1024u;   // 64 MB
constexpr quint32 MaxErrorBytes = 64u * 1024u;           // hata metni tavanı

// ---- byte duzeni -----------------------------------------------------------
inline void putU8(QByteArray& b, quint8 v) { b.append(char(v)); }

inline void putU16(QByteArray& b, quint16 v)
{
    char t[2];
    qToBigEndian(v, t);
    b.append(t, 2);
}

inline void putU32(QByteArray& b, quint32 v)
{
    char t[4];
    qToBigEndian(v, t);
    b.append(t, 4);
}

inline void putU64(QByteArray& b, quint64 v)
{
    char t[8];
    qToBigEndian(v, t);
    b.append(t, 8);
}

inline void putUtf8(QByteArray& b, const QString& s)
{
    const QByteArray utf = s.toUtf8();
    putU32(b, quint32(utf.size()));
    b.append(utf);
}

inline quint16 getBe16(const char* p)
{
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(p));
}

inline quint32 getBe32(const char* p)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(p));
}

inline quint64 getBe64(const char* p)
{
    return qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(p));
}

// ---- istemci -> sunucu mesajlari --------------------------------------------
inline QByteArray header(quint32 id)
{
    QByteArray b;
    putU32(b, id);
    return b;
}

// u8 sikistirma seviyesi (0 = ham) + StringUTF8 yol
inline QByteArray fileListRequest(const QString& path)
{
    QByteArray b = header(FileListRequest);
    putU8(b, 0);
    putUtf8(b, path);
    return b;
}

inline QByteArray downloadStartRequest(const QString& path, quint64 offset)
{
    QByteArray b = header(DownloadStartRequest);
    putUtf8(b, path);
    putU64(b, offset);
    return b;
}

inline QByteArray downloadDataRequest(quint32 size)
{
    QByteArray b = header(DownloadDataRequest);
    putU8(b, 0);              // sikistirma seviyesi: 0 = ham
    putU32(b, size);
    return b;
}

inline QByteArray dirSizeRequest(const QString& path)
{
    QByteArray b = header(DirSizeRequest);
    putUtf8(b, path);
    return b;
}

inline QByteArray uploadStartRequest(const QString& path)
{
    QByteArray b = header(UploadStartRequest);
    putUtf8(b, path);
    putU8(b, 0x1);            // bayrak: dosya varsa ustune yaz
    putU64(b, 0);             // baslangic ofseti
    return b;
}

inline QByteArray uploadDataRequest(const QByteArray& chunk)
{
    QByteArray b = header(UploadDataRequest);
    putU8(b, 0);              // sikistirma seviyesi: 0 = ham
    putU32(b, quint32(chunk.size()));   // sikistirilmis boyut
    putU32(b, quint32(chunk.size()));   // ham boyut (esit = sikistirma yok)
    b.append(chunk);
    return b;
}

// Dosyanin sonunda: u16 dosya bayraklari + u64 degisiklik zamani (ms, epoch)
inline QByteArray uploadEndRequest(quint16 fileFlags, quint64 modTimeMs)
{
    QByteArray b = header(UploadEndRequest);
    putU16(b, fileFlags);
    putU64(b, modTimeMs);
    return b;
}

inline QByteArray mkdirRequest(const QString& path)
{
    QByteArray b = header(MkdirRequest);
    putUtf8(b, path);
    return b;
}

inline QByteArray removeRequest(const QString& path)
{
    QByteArray b = header(RemoveRequest);
    putUtf8(b, path);
    return b;
}

inline QByteArray renameRequest(const QString& oldPath, const QString& newPath)
{
    QByteArray b = header(RenameRequest);
    putUtf8(b, oldPath);
    putUtf8(b, newPath);
    return b;
}

// ---- ayristirma ------------------------------------------------------------
// FILE_LIST_REPLY icerigi: u32 kayit sayisi; kayit basina
// u64 boyut, u64 degisiklik zamani (ms, epoch), u16 bayraklar, StringUTF8 ad.
inline bool parseFileListEntries(const QByteArray& content, QVector<Entry>* entries)
{
    if (content.isEmpty())
        return true;                       // hic kayit yok
    if (content.size() < 4)
        return false;

    const char* p = content.constData();
    qint64 remaining = content.size();
    const quint32 count = getBe32(p);
    p += 4;
    remaining -= 4;

    entries->reserve(entries->size() + int(count));
    for (quint32 i = 0; i < count; ++i) {
        if (remaining < 18)
            return false;
        Entry e;
        e.size = getBe64(p); p += 8;
        const quint64 msecs = getBe64(p); p += 8;
        const quint16 flags = getBe16(p); p += 2;
        remaining -= 18;

        if (remaining < 4)
            return false;
        const quint32 nameLen = getBe32(p);
        p += 4;
        remaining -= 4;
        if (quint64(remaining) < quint64(nameLen))
            return false;
        // Sunucu adlari sonuna NUL (\0) ile yazar (Utf8StringStorage getSize()
        // sonlandirici bayti da sayar); akis dogrulugu icin uzunluk korunur,
        // ad temizlenir. Boylece gezinme yollari da NUL icermemis olur.
        QByteArray rawName(p, int(nameLen));
        while (!rawName.isEmpty() && rawName.endsWith('\0'))
            rawName.chop(1);
        e.name = QString::fromUtf8(rawName);
        p += nameLen;
        remaining -= qint64(nameLen);
        if (e.name.isEmpty())
            continue;                  // bos ad: kayit atlanir, akis korunur

        e.isDir = (flags & FlagDirectory) != 0;
        e.isExe = (flags & FlagExe) != 0;
        if (msecs != 0)
            e.modified = QDateTime::fromMSecsSinceEpoch(qint64(msecs));
        entries->append(e);
    }
    return true;
}

// StringUTF8 okuma: 4 bayt uzunluk + veri (govde basinda).
inline bool parseUtf8(const QByteArray& body, int offset, QString* text)
{
    if (body.size() - offset < 4)
        return false;
    const char* p = body.constData() + offset;
    const quint32 len = getBe32(p);
    if (quint64(body.size()) - quint64(offset) - 4 < quint64(len))
        return false;
    // Sunucu metinleri de sonuna NUL ile yazilir (bkz. parseFileListEntries).
    QByteArray raw(p + 4, int(len));
    while (!raw.isEmpty() && raw.endsWith('\0'))
        raw.chop(1);
    *text = QString::fromUtf8(raw);
    return true;
}

// Uzak yolu protokol yoluna cevir ("C:/Users" -> "/C:/Users"; "/" kalir).
inline QString toWirePath(const QString& appPath)
{
    if (appPath.isEmpty() || appPath == QLatin1String("/"))
        return QStringLiteral("/");
    if (appPath.startsWith(QLatin1Char('/')))
        return appPath;
    return QLatin1Char('/') + appPath;
}

// Protokol yolunu uygulama yoluna cevir ("/C:/Users" -> "C:/Users"; "/" kalir).
inline QString fromWirePath(const QString& wirePath)
{
    QString p = wirePath;
    while (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);
    return p.isEmpty() ? QStringLiteral("/") : p;
}

// Log icin kisa ad
inline const char* messageIdName(quint32 id)
{
    switch (id) {
    case CompressionSupportRequest: return "CompressionSupportRequest";
    case CompressionSupportReply:   return "CompressionSupportReply";
    case FileListRequest:           return "FileListRequest";
    case FileListReply:             return "FileListReply";
    case UploadStartRequest:        return "UploadStartRequest";
    case UploadStartReply:          return "UploadStartReply";
    case UploadDataRequest:         return "UploadDataRequest";
    case UploadDataReply:           return "UploadDataReply";
    case UploadEndRequest:          return "UploadEndRequest";
    case UploadEndReply:            return "UploadEndReply";
    case DownloadStartRequest:      return "DownloadStartRequest";
    case DownloadStartReply:        return "DownloadStartReply";
    case DownloadDataRequest:       return "DownloadDataRequest";
    case DownloadDataReply:         return "DownloadDataReply";
    case DownloadEndReply:          return "DownloadEndReply";
    case MkdirRequest:              return "MkdirRequest";
    case MkdirReply:                return "MkdirReply";
    case RemoveRequest:             return "RemoveRequest";
    case RemoveReply:               return "RemoveReply";
    case RenameRequest:             return "RenameRequest";
    case RenameReply:               return "RenameReply";
    case DirSizeRequest:            return "DirSizeRequest";
    case DirSizeReply:              return "DirSizeReply";
    case LastRequestFailedReply:    return "LastRequestFailed";
    default:                        return "?";
    }
}

} // namespace Ft
