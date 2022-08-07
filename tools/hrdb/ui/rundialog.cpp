#include "rundialog.h"
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>

#include <QtGlobal> // for Q_OS_MACOS
#include "../models/launcher.h"
#include "../models/session.h"
#include "quicklayout.h"

#ifdef Q_OS_MACOS
#define USE_MAC_BUNDLE
#endif

#ifdef USE_MAC_BUNDLE
static QString FindExecutable(const QString& basePath)
{
    QDir baseDir(basePath);
    baseDir.cd("Contents");
    baseDir.cd("MacOS");

    // Read this directory
    //baseDir.setFilter(QDir::Executable);
    baseDir.setFilter(QDir::Files | QDir::Executable);
    QFileInfoList exes = baseDir.entryInfoList();

    if (exes.length() != 0)
        return exes[0].absoluteFilePath();

    return basePath;
}
#endif

RunDialog::RunDialog(QWidget *parent, Session* pSession) :
    QDialog(parent),
    m_pSession(pSession)
{
    this->setObjectName("RunDialog");
    this->setWindowTitle(tr("Launch Hatari"));

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

    QPushButton* pExeButton = new QPushButton(tr("Browse..."), this);
    QPushButton* pPrgButton = new QPushButton(tr("Browse..."), this);
    QPushButton* pWDButton = new QPushButton(tr("Browse..."), this);
    QPushButton* pWatcherButton = new QPushButton(tr("Browse..."), this);
    m_pWatcherCheckBox = new QCheckBox(tr("Watch changes"), this);
    m_pWatcherCheckBox->setLayoutDirection(Qt::LayoutDirection::RightToLeft);
    m_pWatcherCheckBox->setToolTip(tr("Watch this files/folders for changes and reset hatari if changed"));

    m_pExecutableTextEdit = new QLineEdit("hatari", this);
    m_pArgsTextEdit = new QLineEdit("", this);
    m_pPrgTextEdit = new QLineEdit("", this);
    m_pWorkingDirectoryTextEdit = new QLineEdit("", this);
    m_pWatcherFilesTextEdit = new QLineEdit(this);
    m_pWatcherFilesTextEdit->setPlaceholderText("<watch run programm/image>");

    m_pBreakModeCombo = new QComboBox(this);
    m_pBreakModeCombo->addItem(tr("None"), LaunchSettings::BreakMode::kNone);
    m_pBreakModeCombo->addItem(tr("Boot"), LaunchSettings::BreakMode::kBoot);
    m_pBreakModeCombo->addItem(tr("Program Start"), LaunchSettings::BreakMode::kProgStart);
    m_pBreakModeCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    QLabel* pArgumentLink = new QLabel(this);
    pArgumentLink->setText("<a href=\"https://hatari.tuxfamily.org/doc/manual.html#Command_line_options_and_arguments\">Info...</a>");
    pArgumentLink->setOpenExternalLinks(true);
    pArgumentLink->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard|Qt::LinksAccessibleByMouse);
    pArgumentLink->setTextFormat(Qt::RichText);

    int row = 0;
    gridLayout->addWidget(new QLabel(tr("Executable:"), this), row, 0);
    gridLayout->addWidget(m_pExecutableTextEdit, row, 2);
    gridLayout->addWidget(pExeButton, row, 4);
    ++row;

    gridLayout->addWidget(new QLabel(tr("Run Program/Image:"), this), row, 0);
    gridLayout->addWidget(m_pPrgTextEdit, row, 2);
    gridLayout->addWidget(pPrgButton, row, 4);
    ++row;

    //gridLayout->addWidget(new QLabel(tr("Watch:"), this), row, 0);
    gridLayout->addWidget(m_pWatcherCheckBox, row, 0);
    gridLayout->addWidget(m_pWatcherFilesTextEdit, row, 2);
    gridLayout->addWidget(pWatcherButton, row, 4);
    ++row;

    gridLayout->addWidget(new QLabel("Additional options:", this), row, 0);
    gridLayout->addWidget(m_pArgsTextEdit, row, 2);
    gridLayout->addWidget(pArgumentLink, row, 4);
    ++row;

    gridLayout->addWidget(new QLabel(tr("Working Directory:"), this), row, 0);
    gridLayout->addWidget(m_pWorkingDirectoryTextEdit, row, 2);
    gridLayout->addWidget(pWDButton, row, 4);
    ++row;

    gridLayout->addWidget(new QLabel(tr("Break at:"), this), row, 0);
    gridLayout->addWidget(m_pBreakModeCombo, row, 2);
    ++row;

    gridLayout->setColumnStretch(2, 20);
    gridGroupBox->setLayout(gridLayout);

    // Overall layout (options at top, buttons at bottom)
    QVBoxLayout* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(gridGroupBox);
    pLayout->addWidget(pButtonContainer);

    connect(pExeButton, &QPushButton::clicked, this, &RunDialog::exeClicked);
    connect(pPrgButton, &QPushButton::clicked, this, &RunDialog::prgClicked);
    connect(pWDButton, &QPushButton::clicked, this, &RunDialog::workingDirectoryClicked);
    connect(pWatcherButton, &QPushButton::clicked, this, &RunDialog::watcherFilesClicked);
    connect(m_pWatcherFilesTextEdit, &QLineEdit::textChanged, this, &RunDialog::watcherTextChanged);
    connect(m_pWatcherCheckBox, &QCheckBox::stateChanged, this, &RunDialog::watcherActiveChanged);
    connect(pOkButton, &QPushButton::clicked, this, &RunDialog::okClicked);
    connect(pCancelButton, &QPushButton::clicked, this, &RunDialog::reject);
    loadSettings();
    this->setLayout(pLayout);
}

RunDialog::~RunDialog()
{

}

void RunDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup("RunDialog");
    restoreGeometry(settings.value("geometry").toByteArray());
    settings.endGroup();

    // Take a copy of existing settings internally
    m_launchSettings = m_pSession->GetLaunchSettings();

    // Update UI from these settings
    m_pExecutableTextEdit->setText(m_launchSettings.m_hatariFilename);
    m_pPrgTextEdit->setText(m_launchSettings.m_prgFilename);
    m_pArgsTextEdit->setText(m_launchSettings.m_argsTxt);
    m_pWorkingDirectoryTextEdit->setText(m_launchSettings.m_workingDirectory);
    m_pWatcherFilesTextEdit->setText(m_launchSettings.m_watcherFiles);
    m_pWatcherFilesTextEdit->setEnabled(m_launchSettings.m_watcherActive);
    m_pWatcherCheckBox->setCheckState(m_launchSettings.m_watcherActive?Qt::CheckState::Checked:Qt::CheckState::Unchecked);
    m_pBreakModeCombo->setCurrentIndex(m_launchSettings.m_breakMode);
}

void RunDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup("RunDialog");
    settings.setValue("geometry", saveGeometry());
    settings.endGroup();

    // Now write the settings back into the session
    m_pSession->SetLaunchSettings(m_launchSettings);

    // Force serialisation
    m_pSession->saveSettings();
}

void RunDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
}

void RunDialog::closeEvent(QCloseEvent *event)
{
    updateInternalSettingsFromUI();
    saveSettings();
    event->accept();
}

void RunDialog::okClicked()
{
    // update m_launchSettings from UI elements, ready to launch
    updateInternalSettingsFromUI();

    QString prgText = m_pPrgTextEdit->text().trimmed();
    if (prgText.size() != 0)
    {
        QFile prgFile(prgText);
        if (!prgFile.exists())
        {
            QMessageBox::critical(this, "Error", "Program/Image does not exist.");
            return;
        }
    }

    // Sync settings back, whether we succeed or not
    saveSettings();

    // Execute
    bool success = LaunchHatari(m_launchSettings, m_pSession);
    if (success)
    {
        // Force settings save
        accept();       // Close only when successful
    }
    else
    {
        QMessageBox::critical(this, "Error", "Failed to launch.");
    }
}

void RunDialog::exeClicked()
{
    QFileDialog dialog(this,
                       tr("Choose Hatari executable"));
#ifdef USE_MAC_BUNDLE
    dialog.setFileMode(QFileDialog::Directory);
#endif

    QStringList fileNames;
    if (dialog.exec())
    {
        fileNames = dialog.selectedFiles();
        if (fileNames.length() > 0)
        {
            QString name = QDir::toNativeSeparators(fileNames[0]);

#ifdef USE_MAC_BUNDLE
            name = FindExecutable(name);
#endif
            m_pExecutableTextEdit->setText(name);
        }
        updateInternalSettingsFromUI();
    }
}

void RunDialog::prgClicked()
{
    QString filter = "Programs (*.prg *.tos *.ttp *.PRG *.TOS *.TTP);"
            ";Images (*.st *.stx *.msa *.ipf *.ST *.STX *.MSA *.IPF)";
    QString filename = QFileDialog::getOpenFileName(this,
          tr("Choose program or image"),
          QString(), //dir
          filter);
    if (filename.size() != 0)
        m_pPrgTextEdit->setText(QDir::toNativeSeparators(filename));

    m_pWorkingDirectoryTextEdit->setPlaceholderText(m_pPrgTextEdit->text());        
    updateInternalSettingsFromUI();
}

void RunDialog::workingDirectoryClicked()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::Directory);

    QStringList fileNames;
    if (dialog.exec())
    {
        fileNames = dialog.selectedFiles();
        if (fileNames.length() > 0)
            m_pWorkingDirectoryTextEdit->setText(QDir::toNativeSeparators(fileNames[0]));
        updateInternalSettingsFromUI();
    }
}

void RunDialog::watcherFilesClicked()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::ExistingFiles);

    QStringList fileNames;
    if (dialog.exec())
    {
        fileNames = dialog.selectedFiles();
        if (fileNames.length() > 0)
            m_pWatcherFilesTextEdit->setText(QDir::toNativeSeparators(fileNames.join(",")));
        updateInternalSettingsFromUI();
    }
}

void RunDialog::watcherTextChanged()
{
}

void RunDialog::watcherActiveChanged()
{
    if(m_pWatcherCheckBox->checkState()==Qt::CheckState::Checked)
        m_pWatcherFilesTextEdit->setDisabled(false);
    else
        m_pWatcherFilesTextEdit->setDisabled(true);
}

void RunDialog::updateInternalSettingsFromUI()
{
    // Create the launcher settings as temporaries
    m_launchSettings.m_hatariFilename = m_pExecutableTextEdit->text();
    m_launchSettings.m_prgFilename = m_pPrgTextEdit->text().trimmed();
    m_launchSettings.m_argsTxt = m_pArgsTextEdit->text().trimmed();
    m_launchSettings.m_breakMode = static_cast<LaunchSettings::BreakMode>(m_pBreakModeCombo->currentIndex());
    m_launchSettings.m_workingDirectory = m_pWorkingDirectoryTextEdit->text();
    m_launchSettings.m_watcherFiles = m_pWatcherFilesTextEdit->text();
    m_launchSettings.m_watcherActive = m_pWatcherCheckBox->checkState()==Qt::CheckState::Checked?true:false;
    m_launchSettings.m_hatariFilename = m_pExecutableTextEdit->text();
}
