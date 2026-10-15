#include "ConnectionDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

ConnectionDialog::ConnectionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Bağlantı Ayarları"));
    setModal(true);

    m_nameEdit = new QLineEdit(this);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(tr("IP veya bilgisayar adı (örn. 192.168.1.10)"));
    m_groupEdit = new QLineEdit(this);
    m_groupEdit->setPlaceholderText(tr("örn. Saha, Ofis — boş bırakılırsa “Genel” grubu"));
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_savePasswordCheck = new QCheckBox(tr("Şifreyi bağlantı dosyasına yaz (açık metin — dosya paylaşılabilsin)"), this);
    m_savePasswordCheck->setChecked(true);
    m_viewOnlyCheck = new QCheckBox(tr("Yalnızca görüntüle (fare ve klavye kullanılamaz) — önerilen"), this);
    m_viewOnlyCheck->setChecked(true);
    m_sharedCheck = new QCheckBox(tr("Oturumu diğer görüntüleyicilerle paylaş"), this);
    m_sharedCheck->setChecked(true);

    m_scalingCombo = new QComboBox(this);
    m_scalingCombo->addItem(tr("Pencereye sığdır"), static_cast<int>(ScalingMode::FitWindow));
    m_scalingCombo->addItem(tr("Ölçek %100 (1:1)"), static_cast<int>(ScalingMode::ActualSize));
    m_scalingCombo->addItem(tr("Sabit yakınlaştırma"), static_cast<int>(ScalingMode::Zoom));

    m_zoomSpin = new QDoubleSpinBox(this);
    m_zoomSpin->setRange(25.0, 400.0);
    m_zoomSpin->setSingleStep(25.0);
    m_zoomSpin->setDecimals(0);
    m_zoomSpin->setSuffix(tr(" %"));
    m_zoomSpin->setValue(100.0);
    m_zoomSpin->setEnabled(false);
    connect(m_scalingCombo, &QComboBox::currentIndexChanged, m_zoomSpin, [this] {
        m_zoomSpin->setEnabled(m_scalingCombo->currentData().toInt()
                               == static_cast<int>(ScalingMode::Zoom));
    });

    auto* form = new QFormLayout;
    form->addRow(tr("Ad:"), m_nameEdit);
    form->addRow(tr("Sunucu (IP veya ad):"), m_hostEdit);
    form->addRow(tr("Grup:"), m_groupEdit);
    form->addRow(tr("Şifre:"), m_passwordEdit);
    form->addRow(tr("Görüntüleme:"), m_scalingCombo);
    form->addRow(tr("Yakınlaştırma:"), m_zoomSpin);
    form->addRow(QString(), m_savePasswordCheck);
    form->addRow(QString(), m_viewOnlyCheck);
    form->addRow(QString(), m_sharedCheck);

    auto* hint = new QLabel(tr("Not: TightVNC sunucusunda VNC şifresi tanımlı olmalıdır."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_hostEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Eksik bilgi"), tr("Sunucu adresi boş bırakılamaz."));
            m_hostEdit->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    setWindowTitle(tr("Bağlantı Ayarları"));
}

void ConnectionDialog::setProfile(const ConnectionProfile& profile)
{
    m_id = profile.id;
    m_nameEdit->setText(profile.name);
    // Port alanini sormuyoruz; farkli bir port gerekiyorsa "adres:port" yazilir.
    m_hostEdit->setText(profile.port == 5900
                            ? profile.host
                            : QStringLiteral("%1:%2").arg(profile.host).arg(profile.port));
    m_passwordEdit->setText(profile.password);
    m_groupEdit->setText(profile.group);
    m_savePasswordCheck->setChecked(profile.savePassword);
    m_viewOnlyCheck->setChecked(profile.viewOnly);
    m_sharedCheck->setChecked(profile.sharedSession);
    m_scalingCombo->setCurrentIndex(m_scalingCombo->findData(static_cast<int>(profile.scaling)));
    m_zoomSpin->setValue(qRound(profile.zoomFactor * 100.0));
    m_zoomSpin->setEnabled(profile.scaling == ScalingMode::Zoom);
}

ConnectionProfile ConnectionDialog::profile() const
{
    ConnectionProfile p = ConnectionProfile::createNew();
    if (!m_id.isEmpty())
        p.id = m_id;
    p.name = m_nameEdit->text().trimmed();

    // "adres" veya "adres:port" bicimi; birden fazla iki nokta varsa IPv6
    // adresi kabul edilir ve varsayilan port kullanilir.
    QString hostText = m_hostEdit->text().trimmed();
    int port = 5900;
    if (hostText.count(QLatin1Char(':')) == 1) {
        const int colon = hostText.indexOf(QLatin1Char(':'));
        bool ok = false;
        const int value = hostText.mid(colon + 1).toInt(&ok);
        if (ok && value > 0 && value <= 65535) {
            port = value;
            hostText = hostText.left(colon);
        }
    }
    p.host = hostText;
    p.port = static_cast<quint16>(port);

    p.password = m_passwordEdit->text();
    p.group = m_groupEdit->text().trimmed();
    p.savePassword = m_savePasswordCheck->isChecked();
    p.viewOnly = m_viewOnlyCheck->isChecked();
    p.sharedSession = m_sharedCheck->isChecked();
    p.scaling = static_cast<ScalingMode>(m_scalingCombo->currentData().toInt());
    p.zoomFactor = m_zoomSpin->value() / 100.0;
    return p;
}
