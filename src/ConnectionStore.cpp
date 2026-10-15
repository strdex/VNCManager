#include "ConnectionStore.h"
#include "SecretStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcStore, "xvnc.store")

namespace {

QString storePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/connections.json");
}

QString scalingToString(ScalingMode mode)
{
    switch (mode) {
    case ScalingMode::ActualSize: return QStringLiteral("actual");
    case ScalingMode::Zoom: return QStringLiteral("zoom");
    case ScalingMode::FitWindow: break;
    }
    return QStringLiteral("fit");
}

ScalingMode scalingFromString(const QString& text)
{
    if (text == QLatin1String("actual"))
        return ScalingMode::ActualSize;
    if (text == QLatin1String("zoom"))
        return ScalingMode::Zoom;
    return ScalingMode::FitWindow;
}

QJsonObject toJson(const ConnectionProfile& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), p.id);
    o.insert(QStringLiteral("name"), p.name);
    // Eski surumler bu anahtari tanimaz; okuma tarafinda yok sayilir.
    o.insert(QStringLiteral("group"), p.group);
    o.insert(QStringLiteral("host"), p.host);
    o.insert(QStringLiteral("port"), static_cast<int>(p.port));
    o.insert(QStringLiteral("savePassword"), p.savePassword);
    o.insert(QStringLiteral("viewOnly"), p.viewOnly);
    o.insert(QStringLiteral("sharedSession"), p.sharedSession);
    o.insert(QStringLiteral("scaling"), scalingToString(p.scaling));
    o.insert(QStringLiteral("zoom"), p.zoomFactor);
    if (p.savePassword && !p.password.isEmpty()) {
        // Sifre duz metin olarak yazilir; boylece dosya baska makinelere
        // kopyalanip ekip arasinda paylasilabilir.
        o.insert(QStringLiteral("password"), p.password);
    }
    return o;
}

ConnectionProfile fromJson(const QJsonObject& o)
{
    ConnectionProfile p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    // "group" anahtari eski dosyalarda yoktur; o zaman deger bos kalir ve
    // arayuzde kayit varsayilan ("Genel") grubunda gosterilir.
    p.group = o.value(QStringLiteral("group")).toString().trimmed();
    p.host = o.value(QStringLiteral("host")).toString();
    p.port = static_cast<quint16>(o.value(QStringLiteral("port")).toInt(5900));
    p.savePassword = o.value(QStringLiteral("savePassword")).toBool(true);
    p.viewOnly = o.value(QStringLiteral("viewOnly")).toBool(true);
    p.sharedSession = o.value(QStringLiteral("sharedSession")).toBool(true);
    p.scaling = scalingFromString(o.value(QStringLiteral("scaling")).toString());
    p.zoomFactor = o.value(QStringLiteral("zoom")).toDouble(1.0);
    const QString rawPassword = o.value(QStringLiteral("password")).toString();
    // Eski surumlerde sifre DPAPI ile sifrelenmis olabilir; cozulebiliyorsa onu,
    // degilse duz metni kullan.
    const QString legacy = SecretStore::unprotect(rawPassword);
    p.password = legacy.isEmpty() ? rawPassword : legacy;
    if (p.id.isEmpty())
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return p;
}

} // namespace

namespace ConnectionStore {

QString path()
{
    return storePath();
}

QList<ConnectionProfile> load()
{
    QList<ConnectionProfile> profiles;

    QFile file(storePath());
    if (!file.exists())
        return profiles;

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcStore) << "Baglanti dosyasi acilamadi:" << storePath();
        return profiles;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray array = doc.array();
    profiles.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (value.isObject())
            profiles.append(fromJson(value.toObject()));
    }
    return profiles;
}

bool save(const QList<ConnectionProfile>& profiles)
{
    const QString path = storePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(lcStore) << "Klasor olusturulamadi:" << QFileInfo(path).absolutePath();
        return false;
    }

    QJsonArray array;
    for (const ConnectionProfile& p : profiles)
        array.append(toJson(p));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcStore) << "Baglanti dosyasi yazilamadi:" << path;
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace ConnectionStore
