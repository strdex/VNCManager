#pragma once

#include <QString>

// Windows DPAPI (CryptProtectData) ile kullaniciye özel sifreleme.
// Boylece sifreler ayar dosyasinda duz metin olarak durmaz.
namespace SecretStore {

// Duz metni, yalnizca ayni kullanici hesabinda cozulebilen bir blob'a cevirir.
// Hata durumunda bos QString doner.
QString protect(const QString& plain);

// protect() ciktisini duz metne geri dondurur. Hata durumunda bos QString.
QString unprotect(const QString& protectedBase64);

} // namespace SecretStore
