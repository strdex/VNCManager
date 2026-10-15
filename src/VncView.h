#pragma once

#include "ConnectionProfile.h"

#include <QDateTime>
#include <QImage>
#include <QWidget>

class VncSession;

// Uzak masaustunu cizen ve yerel fare/klavye olaylarini sunucuya ileten widget.
class VncView : public QWidget
{
    Q_OBJECT

public:
    explicit VncView(const ConnectionProfile& profile, QWidget* parent = nullptr);
    ~VncView() override;

    QString title() const { return m_title; }
    QString profileId() const { return m_profile.id; }

    ScalingMode scalingMode() const { return m_profile.scaling; }
    void setScalingMode(ScalingMode mode);

    qreal zoomFactor() const { return m_profile.zoomFactor; }
    // Sabit yuzde olcege gecer (ScalingMode::Zoom).
    void setZoom(qreal zoom);
    // Bir sonraki/onceki olcek kademesine gecer.
    void zoomStep(int direction);
    // Ekranda gorunen olcek (FitWindow icin pencereye bagli olarak hesaplanir).
    qreal effectiveScale() const;

    bool isConnected() const { return m_connected; }
    bool viewOnly() const { return m_profile.viewOnly; }
    // Izleme <-> kontrol modu arasinda gecis (sunucuya tus/fare gonderimi).
    void setViewOnly(bool viewOnly);
    // Uzak imleci goruntule: acikken yerel imlec gizlenir, sunucunun
    // framebuffer'a cizdigi imlec gorunur.
    void setRemoteCursorVisible(bool visible);
    bool remoteCursorVisible() const { return m_remoteCursorVisible; }

    const ConnectionProfile& profile() const { return m_profile; }
    VncSession* session() const { return m_session; }
    QString serverDesktopName() const { return m_serverDesktopName; }
    QSize framebufferSize() const { return m_image.size(); }
    QDateTime connectedAt() const { return m_connectedAt; }

    // Menu/ardis tuslarini sunucuya gonderir.
    void sendCtrlAltDelete();
    void sendWindowsKey();

    // Pano: yerel pano icerigini sunucuya gonderir / son uzak pano metnini geri alir.
    // Basariyi bool olarak doner; izleme modunda gonderim yapilmaz.
    bool sendLocalClipboardToRemote();
    bool copyRemoteClipboardToLocal();
    bool hasRemoteClipboard() const { return !m_lastServerClipboard.isEmpty(); }

signals:
    void titleChanged(const QString& title);
    void statusTextChanged(const QString& text);
    void connectionClosed(const QString& reason);
    // Sunucu ile el sikisma tamamlandi (oturum aktif).
    void sessionConnected();
    void scalingChanged(ScalingMode mode, qreal zoom);
    void zoomChanged(qreal scale);
    void viewOnlyChanged(bool viewOnly);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onConnected(const QString& desktopName, int width, int height);
    void onDisconnected(const QString& reason);
    void onRegionUpdated(const QImage& image, int x, int y);
    void onDesktopResized(int width, int height);
    void onServerCutText(const QString& text);
    void onClipboardChanged();
    void onBell();
    void onLogMessage(const QString& message);

private:
    QRectF targetRect() const;
    void updateSizeConstraints();
    QPoint mapToFramebuffer(const QPoint& viewPos) const;
    quint8 currentButtonMask() const;
    void sendPointerAt(const QPoint& viewPos, quint8 extraMask);
    void updateStatusText(const QString& text);

    ConnectionProfile m_profile;
    VncSession* m_session = nullptr;

    QImage m_image;
    QString m_title;
    QString m_statusText;
    bool m_connected = false;
    bool m_remoteCursorVisible = true;
    QPoint m_lastFbPos{-1, -1};   // son gonderilen imlec konumu (viewOnly gecisinde kullanilir)
    QString m_serverDesktopName;
    QDateTime m_connectedAt;

    // Klipboard dongusunu kirpmak icin
    bool m_applyingServerText = false;
    QString m_lastSentClipboard;
    QString m_lastServerClipboard;
};
