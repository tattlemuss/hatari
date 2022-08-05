#include "prefsdialog.h"
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QSettings>
#include <QCloseEvent>
#include <QCheckBox>
#include <QFontDialog>

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
    m_pGraphicsSquarePixels = new QCheckBox(tr("Square Pixels"), this);
    m_pDisassHexNumerics = new QCheckBox(tr("Use hex address register offsets"), this);
    m_pLiveRefresh = new QCheckBox(tr("Live Refresh"), this);

    QLabel* pFont = new QLabel("Font:", this);
    QPushButton* pFontButton = new QPushButton("Select...", this);

    gridGroupBox->setLayout(gridLayout);
    gridLayout->addWidget(m_pGraphicsSquarePixels, 0, 0);
    gridLayout->addWidget(m_pDisassHexNumerics, 1, 0);
    gridLayout->addWidget(m_pLiveRefresh, 2, 0);
    gridLayout->addWidget(pFont, 3, 0);
    gridLayout->addWidget(pFontButton, 3, 1);

    // Overall layout (options at top, buttons at bottom)
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(gridGroupBox);
    pLayout->addWidget(pButtonContainer);

    connect(pOkButton, &QPushButton::clicked, this, &PrefsDialog::okClicked);
    connect(pOkButton, &QPushButton::clicked, this, &PrefsDialog::accept);
    connect(pCancelButton, &QPushButton::clicked, this, &PrefsDialog::reject);

    connect(m_pGraphicsSquarePixels, &QPushButton::clicked, this, &PrefsDialog::squarePixelsClicked);
    connect(m_pDisassHexNumerics, &QPushButton::clicked, this, &PrefsDialog::disassHexNumbersClicked);
    connect(m_pLiveRefresh,          &QPushButton::clicked, this, &PrefsDialog::liveRefreshClicked);
    connect(pFontButton, &QPushButton::clicked, this, &PrefsDialog::fontSelectClicked);
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
        m_settingsCopy.m_font = font;
}

void PrefsDialog::UpdateUIElements()
{
    m_pGraphicsSquarePixels->setChecked(m_settingsCopy.m_bSquarePixels);
    m_pDisassHexNumerics->setChecked(m_settingsCopy.m_bDisassHexNumerics);
    m_pLiveRefresh->setChecked(m_settingsCopy.m_liveRefresh);
}
