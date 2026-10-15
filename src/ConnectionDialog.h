#pragma once

#include "ConnectionProfile.h"

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

// Yeni baglanti olusturma / mevcut baglantiyi duzenleme penceresi.
class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget* parent = nullptr);

    void setProfile(const ConnectionProfile& profile);
    ConnectionProfile profile() const;

private:
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_hostEdit = nullptr;
    QLineEdit* m_groupEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QCheckBox* m_savePasswordCheck = nullptr;
    QCheckBox* m_viewOnlyCheck = nullptr;
    QCheckBox* m_sharedCheck = nullptr;
    QComboBox* m_scalingCombo = nullptr;
    QDoubleSpinBox* m_zoomSpin = nullptr;

    QString m_id;
};
