#include "prefsdialog.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFontDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "../models/session.h"
#include "quicklayout.h"

PrefsDialog::PrefsDialog(QWidget *parent, Session* pSession) :
    QDialog(parent),
    m_pSession(pSession)
{
    this->setObjectName("PrefsDialog");
    this->setWindowTitle(tr("Preferences"));

    // Bottom OK/Cancel buttons
    QPushButton* pOkButton = new QPushButton("&OK", this);
    pOkButton->setDefault(true);
    QPushButton* pCancelButton = new QPushButton("&Cancel", this);
    QHBoxLayout* pHLayout = new QHBoxLayout(this);
    pHLayout->addStretch(20);
    pHLayout->addWidget(pOkButton);
    pHLayout->addWidget(pCancelButton);
    pHLayout->addStretch(20);
    QWidget* pButtonContainer = new QWidget(this);
    pButtonContainer->setLayout(pHLayout);

    // Options grid box
    QGroupBox* gridGroupBox = new QGroupBox(tr("Options"));
    QGridLayout *gridLayout = new QGridLayout;

    gridLayout->setColumnStretch(2, 20);

    // Add the options
    m_pLiveRefresh = new QCheckBox(tr("Live Refresh"), this);
    m_pGraphicsSquarePixels = new QCheckBox(tr("Graphics Inspector: Square Pixels"), this);
    m_pDisassHexNumerics = new QCheckBox(tr("Disassembly: Use hex address register offsets"), this);
    m_pProfileDisplayCombo = new QComboBox(this);
    m_pProfileDisplayCombo->insertItem(Session::Settings::kTotal, "Total");
    m_pProfileDisplayCombo->insertItem(Session::Settings::kMean, "Mean");

    m_pFontLabel = new QLabel("Font:", this);
    QPushButton* pFontButton = new QPushButton("Select...", this);
    QLabel* pProfileDisplay = new QLabel("Disassmbly: Profile Display:", this);

    gridGroupBox->setLayout(gridLayout);
    int row = 0;
    gridLayout->addWidget(m_pLiveRefresh, row++, 0, 1, 2);
    gridLayout->addWidget(m_pGraphicsSquarePixels, row++, 0, 1, 2);
    gridLayout->addWidget(m_pDisassHexNumerics, row++, 0, 1, 2);
    gridLayout->addWidget(pProfileDisplay, row, 0);
    gridLayout->addWidget(m_pProfileDisplayCombo, row++, 1);
    gridLayout->addWidget(m_pFontLabel, row, 0);
    gridLayout->addWidget(pFontButton, row++, 1);

    // Overall layout (options at top, buttons at bottom)
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(gridGroupBox);
    pLayout->addWidget(pButtonContainer);

    connect(pOkButton, &QPushButton::clicked, this, &PrefsDialog::okClicked);
    connect(pOkButton, &QPushButton::clicked, this, &PrefsDialog::accept);
    connect(pCancelButton, &QPushButton::clicked, this, &PrefsDialog::reject);

    connect(m_pGraphicsSquarePixels, &QPushButton::clicked, this, &PrefsDialog::squarePixelsClicked);
    connect(m_pDisassHexNumerics,    &QPushButton::clicked, this, &PrefsDialog::disassHexNumbersClicked);
    connect(m_pLiveRefresh,          &QPushButton::clicked, this, &PrefsDialog::liveRefreshClicked);
    connect(pFontButton,             &QPushButton::clicked, this, &PrefsDialog::fontSelectClicked);

    // Full signal/slot
    connect(m_pProfileDisplayCombo, SIGNAL(currentIndexChanged(int)),         SLOT(profileDisplayChanged(int)));

    loadSettings();
    this->setLayout(pLayout);
}

PrefsDialog::~PrefsDialog()
{

}

void PrefsDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup("PrefsDialog");

    restoreGeometry(settings.value("geometry").toByteArray());
    settings.endGroup();
}

void PrefsDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup("PrefsDialog");

    settings.endGroup();
}

void PrefsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    // Take a copy of the settings
    m_settingsCopy = m_pSession->GetSettings();
    UpdateUIElements();
}

void PrefsDialog::closeEvent(QCloseEvent *event)
{
    // Closing *doesn't* save settings
//    saveSettings();
    event->accept();
}

void PrefsDialog::profileDisplayChanged(int)
{
    m_settingsCopy.m_profileDisplayMode = m_pProfileDisplayCombo->currentIndex();
}

void PrefsDialog::okClicked()
{
    // Write settings back to session (this emits a signal)
    m_pSession->SetSettings(m_settingsCopy);

    // TODO fire a signal
    saveSettings();
}

void PrefsDialog::squarePixelsClicked()
{
    m_settingsCopy.m_bSquarePixels = m_pGraphicsSquarePixels->isChecked();
}

void PrefsDialog::disassHexNumbersClicked()
{
    m_settingsCopy.m_bDisassHexNumerics = m_pDisassHexNumerics->isChecked();
}

void PrefsDialog::liveRefreshClicked()
{
    m_settingsCopy.m_liveRefresh = m_pLiveRefresh->isChecked();
}

void PrefsDialog::fontSelectClicked()
{
    bool ok;
    QFontDialog::FontDialogOptions options;
    options.setFlag(QFontDialog::MonospacedFonts);
    QFont font = QFontDialog::getFont(
                &ok, m_settingsCopy.m_font,
                this,
                "Choose Font",
                options);

    if (ok)
    {
        m_settingsCopy.m_font = font;
        UpdateUIElements();
    }
}

void PrefsDialog::UpdateUIElements()
{
    m_pGraphicsSquarePixels->setChecked(m_settingsCopy.m_bSquarePixels);
    m_pDisassHexNumerics->setChecked(m_settingsCopy.m_bDisassHexNumerics);
    m_pProfileDisplayCombo->setCurrentIndex(m_settingsCopy.m_profileDisplayMode);
    m_pLiveRefresh->setChecked(m_settingsCopy.m_liveRefresh);

    QFontInfo info(m_settingsCopy.m_font);
    m_pFontLabel->setText(QString("Font: " + m_settingsCopy.m_font.family()));
}
