#pragma once

#include <QString>
#include <QUuid>

// Uzak ekrani goruntuleme sekli
enum class ScalingMode {
    FitWindow,  // pencereye sigdir
    ActualSize, // 1:1
    Zoom        // sabit yuzde olcek (zoomFactor)
};

// Bir VNC sunucusuna baglanmak icin gereken tum bilgileri tasiyan kayit.
struct ConnectionProfile {
    QString id;                    // kimlik (QUuid), benzersiz
    QString name;                  // kullaniciya gore ad (bos birakilabilir)
    QString group;                 // grup/lokasyon adi (bos = varsayilan "Genel" grubu)
    QString host;                  // IP veya DNS adi
    quint16 port = 5900;           // VNC portu
    QString password;              // VNC sifresi (connections.json dosyasina duz metin olarak yazilir)
    bool savePassword = true;      // sifre diska kaydedilsin mi
    bool viewOnly = true;          // yalnizca goruntuleme; ilk baglantida izleme modu
    bool sharedSession = true;     // oturum paylasilsin mi (diger viewer'lar gorur)
    ScalingMode scaling = ScalingMode::FitWindow;
    qreal zoomFactor = 1.0;       // ScalingMode::Zoom icin gecerli olcek (1.0 = %100)

    QString address() const
    {
        return QStringLiteral("%1:%2").arg(host).arg(port);
    }

    QString displayName() const
    {
        return name.trimmed().isEmpty() ? address() : name.trimmed();
    }

    // Grup anahtari (bas/son bosluklar temizlenir); bos birakilan kayitlar
    // arayuzde varsayilan kategoriye ("Genel") dusurulur.
    QString groupKey() const
    {
        return group.trimmed();
    }

    static ConnectionProfile createNew()
    {
        ConnectionProfile p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        return p;
    }
};
