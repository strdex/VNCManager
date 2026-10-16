#include "VncView.h"
#include "KeysymMap.h"
#include "VncSession.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

namespace {

constexpr qreal kZoomMin = 0.25;
constexpr qreal kZoomMax = 4.0;

} // namespace

VncView::VncView(const ConnectionProfile& profile, QWidget* parent)
    : QWidget(parent)
    , m_profile(profile)
    , m_title(profile.displayName())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // Ebeveynsiz olusturulur: oturum, gorunun yok olmasindan sonra da
    // arka plan parcacigini tamamlayip kendi kendini imha edebilsin.
    m_session = new VncSession(m_profile);
    connect(m_session, &VncSession::connected, this, &VncView::onConnected);
    connect(m_session, &VncSession::disconnected, this, &VncView::onDisconnected);
    connect(m_session, &VncSession::regionUpdated, this, &VncView::onRegionUpdated);
    connect(m_session, &VncSession::desktopResized, this, &VncView::onDesktopResized);
    connect(m_session, &VncSession::serverCutText, this, &VncView::onServerCutText);
    connect(m_session, &VncSession::bell, this, &VncView::onBell);
    connect(m_session, &VncSession::logMessage, this, &VncView::onLogMessage);

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
            this, &VncView::onClipboardChanged);

    updateStatusText(tr("Bağlanılıyor: %1").arg(m_profile.address()));
    m_session->start();

    // Uzak imleci varsayilan olarak goster: sunucu imleci framebuffer'a cizer,
    // yerel imlec goruntuyu karistirmasin.
    setRemoteCursorVisible(m_remoteCursorVisible);
}

void VncView::setViewOnly(bool viewOnly)
{
    if (m_profile.viewOnly == viewOnly)
        return;

    m_profile.viewOnly = viewOnly;
    if (viewOnly && m_session && m_connected) {
        // Basili kalmis tuslari birak; imlec son bilinen konumda kalsin
        // (0,0 gondermek sunucunun imlecini ekrana tasir).
        if (m_lastFbPos.x() >= 0)
            m_session->sendPointer(m_lastFbPos.x(), m_lastFbPos.y(), 0);
        else
            m_session->sendPointer(-1, -1, 0);   // -1,-1 = imlec hareket etmez
    }
    emit viewOnlyChanged(viewOnly);
    update();
}

void VncView::setRemoteCursorVisible(bool visible)
{
    m_remoteCursorVisible = visible;
    // Sunucu imleci framebuffer'a cizdigi icin yerel imleci gizlemek yeterli.
    setCursor(visible ? QCursor(Qt::BlankCursor) : QCursor(Qt::ArrowCursor));
}

void VncView::sendCtrlAltDelete()
{
    if (!m_session || m_profile.viewOnly)
        return;

    m_session->sendKey(X11Keysym::Control_L, true);
    m_session->sendKey(X11Keysym::Alt_L, true);
    m_session->sendKey(X11Keysym::Delete, true);
    m_session->sendKey(X11Keysym::Delete, false);
    m_session->sendKey(X11Keysym::Alt_L, false);
    m_session->sendKey(X11Keysym::Control_L, false);
}

void VncView::sendWindowsKey()
{
    if (!m_session || m_profile.viewOnly)
        return;

    m_session->sendKey(X11Keysym::Super_L, true);
    m_session->sendKey(X11Keysym::Super_L, false);
}

bool VncView::sendLocalClipboardToRemote()
{
    // Izleme modunda pano degisikligi sunucuya gonderilmez (onClipboardChanged
    // ile ayni kural).
    if (!m_session || !m_connected || m_profile.viewOnly)
        return false;

    const QString text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    if (text.isEmpty())
        return false;

    m_lastSentClipboard = text;
    m_session->sendClipboard(text);
    return true;
}

bool VncView::copyRemoteClipboardToLocal()
{
    if (m_lastServerClipboard.isEmpty())
        return false;

    m_applyingServerText = true;
    QGuiApplication::clipboard()->setText(m_lastServerClipboard, QClipboard::Clipboard);
    m_applyingServerText = false;
    return true;
}

VncView::~VncView()
{
    if (m_session) {
        // Bloklamadan durdur; imhayi oturumun arka plan parcacigi yapar.
        m_session->abandon();
        m_session = nullptr;
    }
}

void VncView::setScalingMode(ScalingMode mode)
{
    if (m_profile.scaling == mode)
        return;

    m_profile.scaling = mode;
    updateSizeConstraints();
    emit scalingChanged(mode, m_profile.zoomFactor);
    emit zoomChanged(effectiveScale());
    updateGeometry();
    update();
}

void VncView::setZoom(qreal zoom)
{
    zoom = qBound(kZoomMin, zoom, kZoomMax);

    const bool modeChanged = m_profile.scaling != ScalingMode::Zoom;
    const bool factorChanged = !qFuzzyCompare(m_profile.zoomFactor, zoom);
    if (!modeChanged && !factorChanged)
        return;

    m_profile.scaling = ScalingMode::Zoom;
    m_profile.zoomFactor = zoom;

    updateSizeConstraints();
    emit scalingChanged(ScalingMode::Zoom, zoom);
    emit zoomChanged(effectiveScale());
    updateGeometry();
    update();
}

void VncView::zoomStep(int direction)
{
    static const qreal levels[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0};
    constexpr int levelCount = int(sizeof(levels) / sizeof(levels[0]));

    const qreal current = effectiveScale();
    if (direction > 0) {
        for (int i = 0; i < levelCount; ++i) {
            if (levels[i] > current + 0.001) {
                setZoom(levels[i]);
                return;
            }
        }
        setZoom(kZoomMax);
    } else {
        for (int i = levelCount - 1; i >= 0; --i) {
            if (levels[i] < current - 0.001) {
                setZoom(levels[i]);
                return;
            }
        }
        setZoom(kZoomMin);
    }
}

qreal VncView::effectiveScale() const
{
    switch (m_profile.scaling) {
    case ScalingMode::ActualSize:
        return 1.0;
    case ScalingMode::Zoom:
        return m_profile.zoomFactor;
    case ScalingMode::FitWindow:
    default:
        if (m_image.isNull() || width() <= 0 || height() <= 0)
            return 1.0;
        return qMin(width() / qreal(m_image.width()), height() / qreal(m_image.height()));
    }
}

void VncView::updateSizeConstraints()
{
    if (m_profile.scaling == ScalingMode::FitWindow || m_image.isNull()) {
        setMinimumSize(QSize());
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        return;
    }

    // 1:1 ve zoom modlarinda boyut icerige sabitlenir; kaydirma cubuklari
    // QScrollArea tarafindan dogru sekilde gorunur.
    const QSize size = targetRect().size().toSize();
    setMinimumSize(size);
    setMaximumSize(size);
}

void VncView::onConnected(const QString& desktopName, int width, int height)
{
    m_connected = true;
    m_image = QImage(width, height, QImage::Format_RGB32);
    m_image.fill(Qt::black);
    updateSizeConstraints();
    updateGeometry();
    emit zoomChanged(effectiveScale());

    m_title = desktopName.isEmpty() ? m_profile.address()
                                    : QStringLiteral("%1 — %2").arg(m_profile.displayName(), desktopName);
    m_serverDesktopName = desktopName;
    m_connectedAt = QDateTime::currentDateTime();
    emit titleChanged(m_title);
    updateStatusText(m_profile.viewOnly ? tr("Yalnızca görüntüleme")
                                        : tr("Bağlandı: %1 x %2").arg(width).arg(height));
    setFocus(Qt::OtherFocusReason);
    update();
    emit sessionConnected();
}

void VncView::onDisconnected(const QString& reason)
{
    m_connected = false;
    updateStatusText(tr("Bağlantı kapandı: %1").arg(reason));
    emit connectionClosed(reason);
    update();
}

void VncView::onRegionUpdated(const QImage& image, int x, int y)
{
    if (m_image.isNull() || image.isNull())
        return;
    if (x < 0 || y < 0 || x + image.width() > m_image.width()
        || y + image.height() > m_image.height())
        return;

    QPainter painter(&m_image);
    painter.drawImage(x, y, image);
    painter.end();

    if (m_profile.scaling == ScalingMode::ActualSize) {
        update(QRect(x, y, image.width(), image.height()));
    } else {
        update();
    }
}

void VncView::onDesktopResized(int width, int height)
{
    QImage fresh(width, height, QImage::Format_RGB32);
    fresh.fill(Qt::black);
    m_image = fresh;

    updateSizeConstraints();
    updateGeometry();
    emit zoomChanged(effectiveScale());
    update();
}

void VncView::onServerCutText(const QString& text)
{
    m_lastServerClipboard = text;
    m_applyingServerText = true;
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
    m_applyingServerText = false;
}

void VncView::onClipboardChanged()
{
    if (m_applyingServerText || m_profile.viewOnly)
        return;

    const QString text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    if (text.isEmpty() || text == m_lastSentClipboard || text == m_lastServerClipboard)
        return;

    m_lastSentClipboard = text;
    if (m_session)
        m_session->sendClipboard(text);
}

void VncView::onBell()
{
    QApplication::beep();
}

void VncView::onLogMessage(const QString& message)
{
    if (!m_connected)
        updateStatusText(message);
}

void VncView::updateStatusText(const QString& text)
{
    m_statusText = text;
    emit statusTextChanged(text);
    update();
}

QRectF VncView::targetRect() const
{
    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0)
        return QRectF(rect());

    switch (m_profile.scaling) {
    case ScalingMode::ActualSize:
        return QRectF(0, 0, m_image.width(), m_image.height());

    case ScalingMode::Zoom:
        return QRectF(0, 0,
                      m_image.width() * m_profile.zoomFactor,
                      m_image.height() * m_profile.zoomFactor);

    case ScalingMode::FitWindow:
    default: {
        const qreal scale = qMin(width() / qreal(m_image.width()),
                                 height() / qreal(m_image.height()));
        const qreal w = m_image.width() * scale;
        const qreal h = m_image.height() * scale;
        return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
    }
    }
}

QPoint VncView::mapToFramebuffer(const QPoint& viewPos) const
{
    if (m_image.isNull())
        return {};

    const QRectF target = targetRect();
    if (target.width() <= 0 || target.height() <= 0)
        return {};

    const qreal nx = (viewPos.x() - target.left()) / target.width();
    const qreal ny = (viewPos.y() - target.top()) / target.height();

    const int x = qBound(0, int(nx * m_image.width()), m_image.width() - 1);
    const int y = qBound(0, int(ny * m_image.height()), m_image.height() - 1);
    return {x, y};
}

quint8 VncView::currentButtonMask() const
{
    quint8 mask = 0;
    const Qt::MouseButtons buttons = QGuiApplication::mouseButtons();
    if (buttons.testFlag(Qt::LeftButton))   mask |= 1;
    if (buttons.testFlag(Qt::MiddleButton)) mask |= 2;
    if (buttons.testFlag(Qt::RightButton))  mask |= 4;
    return mask;
}

void VncView::sendPointerAt(const QPoint& viewPos, quint8 extraMask)
{
    // Izleme modunda fare olaylari (tiklama, surukleme, imlec hareketi)
    // hicbir sekilde sunucuya gonderilmez.
    if (!m_session || !m_connected || m_profile.viewOnly)
        return;

    const QPoint fb = mapToFramebuffer(viewPos);
    m_lastFbPos = fb;
    m_session->sendPointer(fb.x(), fb.y(), quint8(currentButtonMask() | extraMask));
}

void VncView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));

    if (m_image.isNull()) {
        painter.setPen(QColor(0xcc, 0xcc, 0xcc));
        painter.drawText(rect(), Qt::AlignCenter, m_statusText);
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          m_profile.scaling != ScalingMode::ActualSize);
    painter.drawImage(targetRect(), m_image);

    if (!m_connected && !m_statusText.isEmpty()) {
        painter.setPen(QColor(0xcc, 0xcc, 0xcc));
        painter.drawText(rect().adjusted(8, 8, -8, -8),
                         Qt::AlignBottom | Qt::AlignLeft, m_statusText);
    }
}

void VncView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    sendPointerAt(event->position().toPoint(), 0);
    event->accept();
}

void VncView::mouseReleaseEvent(QMouseEvent* event)
{
    sendPointerAt(event->position().toPoint(), 0);
    event->accept();
}

void VncView::mouseMoveEvent(QMouseEvent* event)
{
    sendPointerAt(event->position().toPoint(), 0);
    event->accept();
}

void VncView::wheelEvent(QWheelEvent* event)
{
    // Ctrl + tekerlek: yaklastir/uzaklastir
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        zoomStep(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
        return;
    }

    // Izleme modunda kaydirma da uzak sisteme gonderilmez.
    if (m_session && m_connected && !m_profile.viewOnly) {
        const quint8 wheelMask = event->angleDelta().y() > 0 ? 8 : 16;
        const QPoint fb = mapToFramebuffer(event->position().toPoint());
        m_lastFbPos = fb;
        m_session->sendPointer(fb.x(), fb.y(), quint8(currentButtonMask() | wheelMask));
        m_session->sendPointer(fb.x(), fb.y(), currentButtonMask());
    }
    event->accept();
}

bool VncView::claimsKey(const QKeyEvent* event) const
{
    // Penceredeki kisayollar yalnizca goruntuleyici disi islevler icin
    // ayrilir: F11 (tam ekran) ve pano/dosya transferi kombinasyonlari
    // (Ctrl+Shift+C/V/F). Bunlar disindaki her tus kontrol modunda uzak
    // sisteme gonderilir; boylece Del, Ctrl+C, Ctrl+V, Ctrl+W gibi tuslar
    // uygulama eylemleri tarafindan yutulmaz.
    if (event->key() == Qt::Key_F11)
        return false;

    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods.testFlag(Qt::ControlModifier) && mods.testFlag(Qt::ShiftModifier)) {
        switch (event->key()) {
        case Qt::Key_C:
        case Qt::Key_V:
        case Qt::Key_F:
            return false;
        default:
            break;
        }
    }

    return true;
}

bool VncView::event(QEvent* event)
{
    // Kisayol cakismasi: Qt, bir QAction/QShortcut tetiklenmeden once odak
    // widget'ina ShortcutOverride gonderir. Kontrol modunda bunu kabul ederek
    // penceredeki kisayollarin (orn. Del = "Baglantiyi sil", Ctrl+W = sekme
    // kapat) tusu yutmasi engellenir ve tus keyPressEvent'e ulasir.
    if (event->type() == QEvent::ShortcutOverride
        && m_session && m_connected && !m_profile.viewOnly) {
        if (claimsKey(static_cast<QKeyEvent*>(event))) {
            event->accept();
            return true;
        }
    }

    return QWidget::event(event);
}

void VncView::keyPressEvent(QKeyEvent* event)
{
    if (!m_session || !m_connected || m_profile.viewOnly) {
        QWidget::keyPressEvent(event);
        return;
    }

    const quint32 keysym = keysymForKeyEvent(event);
    if (keysym == 0) {
        QWidget::keyPressEvent(event);
        return;
    }

    m_session->sendKey(keysym, true);
    event->accept();
}

void VncView::keyReleaseEvent(QKeyEvent* event)
{
    if (!m_session || !m_connected || m_profile.viewOnly) {
        QWidget::keyReleaseEvent(event);
        return;
    }

    const quint32 keysym = keysymForKeyEvent(event);
    if (keysym == 0) {
        QWidget::keyReleaseEvent(event);
        return;
    }

    m_session->sendKey(keysym, false);
    event->accept();
}

void VncView::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
}

void VncView::focusOutEvent(QFocusEvent* event)
{
    // Pencere degisince basili kalmis tuslarin birakilmis oldugundan emin ol.
    if (m_session && m_connected)
        m_session->sendPointer(0, 0, 0);

    QWidget::focusOutEvent(event);
}

void VncView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // FitWindow modunda gorunen olcek pencere boyutuna baglidir.
    if (m_profile.scaling == ScalingMode::FitWindow)
        emit zoomChanged(effectiveScale());
}
