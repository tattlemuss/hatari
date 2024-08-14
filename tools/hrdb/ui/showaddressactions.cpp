#include "showaddressactions.h"

#include <QContextMenuEvent>
#include "../models/session.h"

ShowAddressActions::ShowAddressActions() :
    m_activeAddress(0),
    m_memorySpace(Memory::kCpu),
    m_pSession(nullptr)
{
    for (int i = 0; i < kNumDisasmViews; ++i)
        m_pDisasmWindowActions[i] = new QAction(QString::asprintf("Show in Disassembly %d", i + 1), this);

    for (int i = 0; i < kNumMemoryViews; ++i)
        m_pMemoryWindowActions[i] = new QAction(QString::asprintf("Show in Memory %d", i + 1), this);

    m_pGraphicsInspectorAction = new QAction("Show in Graphics Inspector", this);

    for (int i = 0; i < kNumDisasmViews; ++i)
        connect(m_pDisasmWindowActions[i], &QAction::triggered, this, [=] () { this->TriggerDisasmView(i); } );

    for (int i = 0; i < kNumMemoryViews; ++i)
        connect(m_pMemoryWindowActions[i], &QAction::triggered, this, [=] () { this->TriggerMemoryView(i); } );

    connect(m_pGraphicsInspectorAction, &QAction::triggered, this, [=] () { this->TriggerGraphicsInspector(); } );
}

ShowAddressActions::~ShowAddressActions()
{

}

void ShowAddressActions::addActionsToMenu(QMenu* pMenu) const
{
    for (int i = 0; i < kNumDisasmViews; ++i)
        pMenu->addAction(m_pDisasmWindowActions[i]);

    for (int i = 0; i < kNumMemoryViews; ++i)
        pMenu->addAction(m_pMemoryWindowActions[i]);

    pMenu->addAction(m_pGraphicsInspectorAction);
}

void ShowAddressActions::setAddress(Session* pSession, int memorySpace, uint32_t address)
{
    m_activeAddress = address;
    m_memorySpace = memorySpace;
    m_pSession = pSession;

    // Certain window types only accept CPU memory
    // e.g. Graphics Inspector
    bool isCpu = (memorySpace == Memory::kCpu);
    for (int i = 0; i < kNumMemoryViews; ++i)
        m_pMemoryWindowActions[i]->setVisible(isCpu);

    m_pGraphicsInspectorAction->setVisible(isCpu);
}

void ShowAddressActions::TriggerDisasmView(int windowIndex)
{
    emit m_pSession->addressRequested(Session::kDisasmWindow, windowIndex, m_memorySpace, m_activeAddress);
}

void ShowAddressActions::TriggerMemoryView(int windowIndex)
{
    emit m_pSession->addressRequested(Session::kMemoryWindow, windowIndex, m_memorySpace, m_activeAddress);
}

void ShowAddressActions::TriggerGraphicsInspector()
{
    emit m_pSession->addressRequested(Session::kGraphicsInspector, 0, m_memorySpace, m_activeAddress);
}


ShowAddressMenu::ShowAddressMenu()
{
    m_pMenu = new QMenu(nullptr);
    addActionsToMenu(m_pMenu);
}

ShowAddressMenu::~ShowAddressMenu()
{
    delete m_pMenu;
}

ShowAddressLabel::ShowAddressLabel(Session *pSession) :
    m_pActions(nullptr)
{
    m_pActions = new ShowAddressActions();
    m_pActions->setAddress(pSession, Memory::kCpu, 0);
}

ShowAddressLabel::~ShowAddressLabel()
{
    delete m_pActions;
}

void ShowAddressLabel::SetAddress(Session* pSession, int memorySpace, uint32_t address)
{
    this->setText(QString::asprintf("<a href=\"null\">$%x</a>", address));
    this->setTextFormat(Qt::TextFormat::RichText);
    m_pActions->setAddress(pSession, memorySpace, address);
}

void ShowAddressLabel::contextMenuEvent(QContextMenuEvent *event)
{
    // Right click menus are instantiated on demand, so we can
    // dynamically add to them
    QMenu menu(this);
    m_pActions->addActionsToMenu(&menu);
    menu.exec(event->globalPos());
}
