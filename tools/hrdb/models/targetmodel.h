#ifndef TARGET_MODEL_H
#define TARGET_MODEL_H

#include <stdint.h>
#include <QObject>

#include "transport/remotecommand.h"
#include "breakpoint.h"
#include "memory.h"
#include "symboltable.h"
#include "registers.h"
#include "exceptionmask.h"

class QTimer;

enum MACHINETYPE
{
    MACHINE_ST = 0,
    MACHINE_MEGA_ST = 1,
    MACHINE_STE = 2,
    MACHINE_MEGA_STE = 3,
    MACHINE_TT = 4,
    MACHINE_FALCON = 5
};

class TargetChangedFlags
{
public:
    enum ChangedState
    {
        kPC,
        kRegs,
        kBreakpoints,
        kSymbolTable,
        kExceptionMask,
        kOtherMemory,        // e.g. a control set memory elsewhere
        kChangedStateCount
    };

    void Clear();
    void SetChanged(ChangedState ch) { m_changed[ch] = true; }
    void SetMemoryChanged(MemorySlot slot) { m_memChanged[slot] = true; }

    bool    m_changed[kChangedStateCount];
    bool    m_memChanged[kMemorySlotCount];
};

/*
    Core central data model reflecting the state of the target.
*/
class TargetModel : public QObject
{
	Q_OBJECT
public:
	TargetModel();
	virtual ~TargetModel();

    // These are called by the Dispatcher when notifications/events arrive
    void SetConnected(int running);
    void SetStatus(bool running, uint32_t pc);
    void SetConfig(uint32_t machineType, uint32_t cpuLevel);

    // These are called by the Dispatcher when responses arrive
    void SetRegisters(const Registers& regs, uint64_t commandId);
    void SetMemory(MemorySlot slot, const Memory* pMem, uint64_t commandId);
    void SetBreakpoints(const Breakpoints& bps, uint64_t commandId);

    // Set Hatari's subtable of symbols
    void SetSymbolTable(const SymbolSubTable& syms, uint64_t commandId);
    void SetExceptionMask(const ExceptionMask& mask);
    void NotifyMemoryChanged(uint32_t address, uint32_t size);

    // User-added console command. Anything can happen!
    void ConsoleCommand();

    void Flush();

	// NOTE: all these return copies to avoid data contention
    MACHINETYPE	GetMachineType() const { return m_machineType; }

    int IsConnected() const { return m_bConnected; }
    int IsRunning() const { return m_bRunning; }
	uint32_t GetPC() const { return m_pc; }
	Registers GetRegs() const { return m_regs; }
    const Memory* GetMemory(MemorySlot slot) const
    {
        return m_pMemory[slot];
    }
    const Breakpoints& GetBreakpoints() const { return m_breakpoints; }
    const SymbolTable& GetSymbolTable() const { return m_symbolTable; }
    const ExceptionMask& GetExceptionMask() const { return m_exceptionMask; }

public slots:

signals:
    // connect/disconnect change
    void connectChangedSignal();

    // When start/stop status is changed
    void startStopChangedSignal();

    void startStopChangedSignalDelayed(int running);

    void changedFlush(const TargetChangedFlags& flags);

	// When new CPU registers are changed
    void registersChangedSignal(uint64_t commandId);

	// When a block of fetched memory is changed
	// TODO: don't have every view listening to the same slot?
    void memoryChangedSignal(int memorySlot, uint64_t commandId);

    void breakpointsChangedSignal(uint64_t commandId);
    void symbolTableChangedSignal(uint64_t commandId);
    void exceptionMaskChanged();

    // UI BODGE
    // Qt seems to have no central message dispatch
    void addressRequested(int windowId, bool isMemory, uint32_t address);

    // Something edited memory
    void otherMemoryChanged(uint32_t address, uint32_t size);

private slots:

    // Called shortly after stop notification received
    void delayedTimer();

private:
    TargetChangedFlags  m_changedFlags;

    MACHINETYPE     m_machineType;	// Hatari MACHINETYPE enum
    uint32_t        m_cpuLevel;		// CPU 0=000, 1=010, 2=020, 3=030, 4=040, 5=060

    int             m_bConnected;   // 0 == disconnected, 1 == connected
    int             m_bRunning;		// 0 == stopped, 1 == running
    uint32_t        m_pc;			// PC register (for next instruction)

    Registers       m_regs;			// Current register values
    Breakpoints     m_breakpoints;  // Current breakpoint list
    SymbolTable     m_symbolTable;
    ExceptionMask   m_exceptionMask;

    // Actual current memory contents
    const Memory*   m_pMemory[MemorySlot::kMemorySlotCount];

    // Timer running to trigger events after CPU has stopped for a while
    // (e.g. Graphics Inspector refresh)
    QTimer*         m_pDelayedUpdateTimer;
};

// Helper functions to check broad machine types
extern bool IsMachineST(MACHINETYPE type);

#endif // TARGET_MODEL_H
