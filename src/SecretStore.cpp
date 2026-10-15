#include "SecretStore.h"

#include <QByteArray>
#include <QStringConverter>

#include <windows.h>
#include <dpapi.h>

namespace {

DATA_BLOB toBlob(const QByteArray& data)
{
    DATA_BLOB blob;
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(data.constData()));
    blob.cbData = static_cast<DWORD>(data.size());
    return blob;
}

} // namespace

namespace SecretStore {

QString protect(const QString& plain)
{
    const QByteArray utf8 = plain.toUtf8();
    DATA_BLOB in = toBlob(utf8);
    DATA_BLOB out = {};

    if (!CryptProtectData(&in, L"xVNCManager", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }

    const QByteArray result(reinterpret_cast<const char*>(out.pbData),
                            static_cast<qsizetype>(out.cbData));
    LocalFree(out.pbData);
    return QString::fromLatin1(result.toBase64());
}

QString unprotect(const QString& protectedBase64)
{
    if (protectedBase64.isEmpty())
        return {};

    const QByteArray blob = QByteArray::fromBase64(protectedBase64.toLatin1());
    if (blob.isEmpty())
        return {};

    DATA_BLOB in = toBlob(blob);
    DATA_BLOB out = {};

    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }

    const QString result = QString::fromUtf8(reinterpret_cast<const char*>(out.pbData),
                                             static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

} // namespace SecretStore
