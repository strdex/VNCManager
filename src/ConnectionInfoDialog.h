#pragma once

#include "ConnectionProfile.h"

#include <QDialog>

class QLabel;
class QTimer;
class VncView;

// Aktif oturumun ozetini gosteren pencere (sure bilgisi canli guncellenir).
class ConnectionInfoDialog : public QDialog
{
    Q_OBJECT

public:
    ConnectionInfoDialog(const ConnectionProfile& profile, VncView* view,
                         QWidget* parent = nullptr);

private:
    void refresh();

    ConnectionProfile m_profile;
    VncView* m_view = nullptr;

    QLabel* m_nameLabel = nullptr;
    QLabel* m_addressLabel = nullptr;
    QLabel* m_groupLabel = nullptr;
    QLabel* m_desktopLabel = nullptr;
    QLabel* m_resolutionLabel = nullptr;
    QLabel* m_modeLabel = nullptr;
    QLabel* m_inputLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    QTimer* m_timer = nullptr;
};
