#pragma once

#include "ConnectionProfile.h"

#include <QList>

// Baglanti kayitlarinin saklandigi JSON dosyasinin yuklenmesi/yazilmasi.
namespace ConnectionStore {

// connections.json dosyasinin tam yolu (varsayilan uygulama veri klasoru).
QString path();

QList<ConnectionProfile> load();
bool save(const QList<ConnectionProfile>& profiles);

} // namespace ConnectionStore
