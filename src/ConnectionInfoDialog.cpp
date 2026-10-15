#include "ConnectionInfoDialog.h"

#include "VncView.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString scalingText(const VncView* view)
{
    switch (view->scalingMode()) {
    case ScalingMode::FitWindow:
        return QObject::tr("Pencereye sığdır (%1%)").arg(qRound(view->effectiveScale() * 100));
    case ScalingMode::ActualSize:
        return QObject::tr("Tam görüntüleme, 1:1");
    case ScalingMode::Zoom:
        return QObject::tr("Sabit yakınlaştırma (%1%)")
            .arg(qRound(view->effectiveScale() * 100));
    }
    return {};
}

QString durationText(qint64 seconds)
{
    if (seconds < 0)
        return QObject::tr("—");

    const qint64 hours = seconds / 3600;
    const int minutes = int((seconds % 3600) / 60);
    const int secs = int(seconds % 60);

    if (hours > 0)
        return QObject::tr("%1 sa %2 dk %3 sn").arg(hours).arg(minutes).arg(secs);
    if (minutes > 0)
        return QObject::tr("%1 dk %2 sn").arg(minutes).arg(secs);
    return QObject::tr("%1 sn").arg(secs);
}

} // namespace

ConnectionInfoDialog::ConnectionInfoDialog(const ConnectionProfile& profile,
                                           VncView* view, QWidget* parent)
    : QDialog(parent)
    , m_profile(profile)
    , m_view(view)
{
    setWindowTitle(tr("Bağlantı Bilgisi — %1").arg(profile.displayName()));
    setModal(true);

    m_nameLabel = new QLabel(this);
    m_addressLabel = new QLabel(this);
    m_groupLabel = new QLabel(this);
    m_desktopLabel = new QLabel(this);
    m_resolutionLabel = new QLabel(this);
    m_modeLabel = new QLabel(this);
    m_inputLabel = new QLabel(this);
    m_cursorLabel = new QLabel(this);
    m_statusLabel = new QLabel(this);
    m_durationLabel = new QLabel(this);

    auto* form = new QFormLayout;
    form->addRow(tr("Cihaz:"), m_nameLabel);
    form->addRow(tr("Sunucu:"), m_addressLabel);
    form->addRow(tr("Grup:"), m_groupLabel);
    form->addRow(tr("Masaüstü:"), m_desktopLabel);
    form->addRow(tr("Çözünürlük:"), m_resolutionLabel);
    form->addRow(tr("Görüntüleme:"), m_modeLabel);
    form->addRow(tr("Girdi:"), m_inputLabel);
    form->addRow(tr("Uzak imleç:"), m_cursorLabel);
    form->addRow(tr("Durum:"), m_statusLabel);
    form->addRow(tr("Oturum süresi:"), m_durationLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &ConnectionInfoDialog::refresh);
    m_timer->start();

    refresh();
}

void ConnectionInfoDialog::refresh()
{
    m_nameLabel->setText(m_profile.displayName());
    m_addressLabel->setText(m_profile.address());
    m_groupLabel->setText(m_profile.groupKey().isEmpty() ? tr("Genel")
                                                         : m_profile.groupKey());

    if (!m_view) {
        m_desktopLabel->setText(tr("—"));
        m_resolutionLabel->setText(tr("—"));
        m_modeLabel->setText(tr("—"));
        m_inputLabel->setText(tr("—"));
        m_cursorLabel->setText(tr("—"));
        m_statusLabel->setText(tr("Oturum kapandı"));
        m_durationLabel->setText(tr("—"));
        return;
    }

    m_desktopLabel->setText(m_view->serverDesktopName().isEmpty()
                                ? tr("(bağlanıyor)")
                                : m_view->serverDesktopName());

    const QSize size = m_view->framebufferSize();
    m_resolutionLabel->setText(size.isValid() && !size.isEmpty()
                                   ? QStringLiteral("%1 x %2").arg(size.width()).arg(size.height())
                                   : tr("(bağlanıyor)"));

    m_modeLabel->setText(scalingText(m_view));
    m_inputLabel->setText(m_view->viewOnly() ? tr("Yalnızca görüntüleme (izleme)")
                                             : tr("Kontrol (fare ve klavye aktif)"));
    m_cursorLabel->setText(m_view->remoteCursorVisible()
                               ? tr("Gösteriliyor (yerel imleç gizli)")
                               : tr("Gizli (yerel imleç görünür)"));
    m_statusLabel->setText(m_view->isConnected() ? tr("Bağlı") : tr("Kopuk"));

    const QDateTime at = m_view->connectedAt();
    m_durationLabel->setText(at.isValid()
                                 ? durationText(at.secsTo(QDateTime::currentDateTime()))
                                 : tr("—"));
}
