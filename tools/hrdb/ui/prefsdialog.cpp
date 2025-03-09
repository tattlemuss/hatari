#include "prefsdialog.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
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

    QTabWidget* pTabs = new QTabWidget(this);

    // Options #1 grid box
    QGroupBox* gridGroupBox1 = new QGroupBox();
    QGridLayout *gridLayout1 = new QGridLayout;
    QGroupBox* gridGroupBox2 = new QGroupBox();
    QGridLayout *gridLayout2 = new QGridLayout;
    gridGroupBox1->setLayout(gridLayout1);
    gridGroupBox2->setLayout(gridLayout2);

    // Add the options
    gridLayout1->setColumnStretch(2, 20);
    m_pLiveRefresh = new QCheckBox(tr("Live Refresh"), this);
    m_pGraphicsSquarePixels = new QCheckBox(tr("Graphics Inspector: Square Pixels"), this);
    m_pDisassHexNumerics = new QCheckBox(tr("Disassembly: Use hex address register offsets"), this);
    m_pProfileDisplayCombo = new QComboBox(this);
    m_pProfileDisplayCombo->insertItem(Session::Settings::kTotal, "Total");
    m_pProfileDisplayCombo->insertItem(Session::Settings::kMean, "Mean");

    m_pFontLabel = new QLabel("Font:", this);
    QPushButton* pFontButton = new QPushButton("Select...", this);
    QLabel* pProfileDisplay = new QLabel("Disassembly: Profile Display:", this);

    // Tab 1
    int row = 0;
    gridLayout1->addWidget(m_pLiveRefresh, row++, 0, 1, 2);
    gridLayout1->addWidget(m_pGraphicsSquarePixels, row++, 0, 1, 2);
    gridLayout1->addWidget(m_pDisassHexNumerics, row++, 0, 1, 2);
    gridLayout1->addWidget(pProfileDisplay, row, 0);
    gridLayout1->addWidget(m_pProfileDisplayCombo, row++, 1);
    gridLayout1->addWidget(m_pFontLabel, row, 0);
    gridLayout1->addWidget(pFontButton, row++, 1);

    // Tab 2
    row = 0;
    gridLayout2->setColumnStretch(1, 20);
    gridLayout2->addWidget(new QLabel("Source code search directories"), row++, 0, 1, 1);
    for (int i = 0; i < Session::Settings::kNumSearchDirectories; ++i)
    {
        QLabel* pLabel = new QLabel(QString::asprintf("Directory #%d", i+1), this);
        m_pSourceDirTextEdit[i] = new QLineEdit(this);
        m_pSourceDirButton[i] = new QPushButton(tr("Browse..."), this);

        gridLayout2->addWidget(pLabel, row, 0, 1, 1);
        gridLayout2->addWidget(m_pSourceDirTextEdit[i], row, 1, 1, 1);
        gridLayout2->addWidget(m_pSourceDirButton[i], row, 2, 1, 1);
        connect(m_pSourceDirButton[i], &QPushButton::clicked, this, [=] () { this->ChooseSourceDir(i); } );
        ++row;
    }

    pTabs->addTab(gridGroupBox1, "Appearance");
    pTabs->addTab(gridGroupBox2, "Source");

    // Overall layout (options at top, buttons at bottom)
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(pTabs);
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

void PrefsDialog::ChooseSourceDir(int index)
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setDirectory(m_settingsCopy.m_sourceSearchDirectories[index]);
    QStringList fileNames;
    if (dialog.exec())
    {
        fileNames = dialog.selectedFiles();
        if (fileNames.length() > 0)
            m_settingsCopy.m_sourceSearchDirectories[index] = fileNames[0];
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

    for (int i = 0; i < Session::Settings::kNumSearchDirectories; ++i)
        m_pSourceDirTextEdit[i]->setText(m_settingsCopy.m_sourceSearchDirectories[i]);
}
