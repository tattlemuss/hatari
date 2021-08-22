#include "rundialog.h"
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QPushButton>
#include <QComboBox>
#include <QTemporaryFile>
#include <QTextStream>

#include "../models/session.h"
#include "quicklayout.h"

RunDialog::RunDialog(QWidget *parent, Session* pSession) :
    QDialog(parent),
    m_pSession(pSession)
{
    this->setObjectName("RunDialog");
    this->setWindowTitle(tr("Run Hatari"));

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
    QGroupBox* gridGroupBox = new QGroupBox(tr("Launch options"));
    QGridLayout *gridLayout = new QGridLayout;

    QPushButton* pExeButton = new QPushButton(tr("Browse..."), this);
    QPushButton* pPrgButton = new QPushButton(tr("Browse..."), this);
    QPushButton* pWDButton = new QPushButton(tr("Browse..."), this);

    m_pExecutableTextEdit = new QLineEdit("hatari", this);
    m_pArgsTextEdit = new QLineEdit("", this);
    m_pPrgTextEdit = new QLineEdit("", this);
    m_pWorkingDirectoryTextEdit = new QLineEdit("", this);
    m_pBreakModeCombo = new QComboBox(this);
    m_pBreakModeCombo->addItem(tr("None"), BreakMode::kNone);
    m_pBreakModeCombo->addItem(tr("Boot"), BreakMode::kBoot);
    m_pBreakModeCombo->addItem(tr("Program Start"), BreakMode::kProgStart);
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
    connect(pOkButton, &QPushButton::clicked, this, &RunDialog::okClicked);
    connect(pOkButton, &QPushButton::clicked, this, &RunDialog::accept);
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
    m_pExecutableTextEdit->setText(settings.value("exe", QVariant("hatari")).toString());
    m_pArgsTextEdit->setText(settings.value("args", QVariant("")).toString());
    m_pPrgTextEdit->setText(settings.value("prg", QVariant("")).toString());
    m_pWorkingDirectoryTextEdit->setText(settings.value("workingDirectory", QVariant("")).toString());
    m_pBreakModeCombo->setCurrentIndex(settings.value("breakMode", QVariant("0")).toInt());
    settings.endGroup();
}

void RunDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup("RunDialog");

    settings.setValue("geometry", saveGeometry());
    settings.setValue("exe", m_pExecutableTextEdit->text());
    settings.setValue("args", m_pArgsTextEdit->text());
    settings.setValue("prg", m_pPrgTextEdit->text());
    settings.setValue("workingDirectory", m_pWorkingDirectoryTextEdit->text());
    settings.setValue("breakMode", m_pBreakModeCombo->currentIndex());
    settings.endGroup();
}

void RunDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
}

void RunDialog::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}

void RunDialog::okClicked()
{
    QStringList args;

    // Start with the "other args" then add the rest around it
    QString otherArgsText = m_pArgsTextEdit->text();
    otherArgsText = otherArgsText.trimmed();
    if (otherArgsText.size() != 0)
        args = otherArgsText.split(" ");

    // First make a temp file for breakpoints etc
    BreakMode breakMode = (BreakMode) m_pBreakModeCombo->currentIndex();
    if (breakMode != BreakMode::kNone)
    {
        QString tmpContents;
        QTextStream ref(&tmpContents);

        // Generate some commands for
        // Break at boot/start commands
        if (breakMode == BreakMode::kBoot)
            ref << QString("b pc ! 0 : once\r\n");
        else if (breakMode == BreakMode::kProgStart)
            ref << QString("b pc=TEXT && pc<$e00000 : once\r\n");

        // Create the temp file
        // In theory we need to be careful about reuse?
        QTemporaryFile& tmp(*m_pSession->m_pStartupFile);
        if (!tmp.open())
            return;

        tmp.setTextModeEnabled(true);
        tmp.write(tmpContents.toUtf8());
        tmp.close();

        // Prepend the "--parse N" part (backwards!)
        args.push_front(tmp.fileName());
        args.push_front("--parse");
    }

    QString prgText = m_pPrgTextEdit->text().trimmed();
    if (prgText.size() != 0)
        args.push_back(prgText);

    // Actually launch the program
    QProcess proc;
    proc.setProgram(m_pExecutableTextEdit->text());
    proc.setArguments(args);
    proc.setWorkingDirectory(m_pWorkingDirectoryTextEdit->text());
    proc.startDetached();

    saveSettings();
}

void RunDialog::exeClicked()
{
    QString filename = QFileDialog::getOpenFileName(this,
          tr("Choose Hatari executable"));
    if (filename.size() != 0)
        m_pExecutableTextEdit->setText(QDir::toNativeSeparators(filename));
    saveSettings();
}

void RunDialog::prgClicked()
{
    QString filter = "Programs (*.prg *.tos *.ttp);;Images (*.st *.stx *.msa *.ipf)";
    QString filename = QFileDialog::getOpenFileName(this,
          tr("Choose program or image"),
          QString(), //dir
          filter);
    if (filename.size() != 0)
        m_pPrgTextEdit->setText(QDir::toNativeSeparators(filename));
    saveSettings();
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
        saveSettings();
    }
}

