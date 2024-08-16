#ifndef SHOWADDRESSACTIONS_H
#define SHOWADDRESSACTIONS_H

#include <QMenu>
#include <QLabel>
#include "../models/memory.h"

class QAction;
class Session;

// Contains the QActions to do "Show in Disassembly 1", "Show in Memory 2" etc,
// plus handling their callbacks/triggers
class ShowAddressActions : public QObject
{
    Q_OBJECT
public:
    ShowAddressActions();
    virtual ~ShowAddressActions();
    void addActionsToMenu(QMenu* pMenu) const;
    void setAddress(Session* pSession, int memorySpace, uint32_t address);

private:
    // Functions (invoked via lambdas) when "show in Memory X" etc is selected
    void TriggerDisasmView(int windowIndex);
    void TriggerMemoryView(int windowIndex);
    void TriggerGraphicsInspector();

    // What address will be set to the Window chosen
    uint32_t     m_activeAddress;
    int          m_memorySpace;     // see Memory::Space

    // Actions to add to the menus
    QAction*     m_pDisasmWindowActions[kNumDisasmViews];
    QAction*     m_pMemoryWindowActions[kNumMemoryViews];
    QAction*     m_pGraphicsInspectorAction;

    // Pointer for signal sending
    Session*     m_pSession;
};

// Contains the ShowAddressActions, plus a menu item
class ShowAddressMenu : public ShowAddressActions
{
public:
    ShowAddressMenu();
    ~ShowAddressMenu();

    // Set the label of the whole menu (the address is added automatically),
    // and set up the sub-actions to point to the correct address.
    void Set(const QString& title, Session* pSession, int memorySpace, uint32_t address);

    QMenu*      m_pMenu;
};

class ShowAddressLabel : public QLabel
{
public:
    ShowAddressLabel(Session* pSession);
    ~ShowAddressLabel();

    void SetAddress(Session* pSession, int memorySpace, uint32_t address);
    void contextMenuEvent(QContextMenuEvent *event);

    ShowAddressActions*      m_pActions;
};

#endif // SHOWADDRESSACTIONS_H
