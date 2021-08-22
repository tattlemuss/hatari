#include "targetmodel.h"

#include <iostream>
#include <QTimer>

void TargetChangedFlags::Clear()
{
    for (int i = 0; i < kChangedStateCount; ++i)
        m_changed[i] = false;

    for (int i = 0; i < kMemorySlotCount; ++i)
        m_memChanged[i] = false;
}

TargetModel::TargetModel() :
	QObject(),
    m_bConnected(false),
    m_bRunning(true)
{
    for (int i = 0; i < MemorySlot::kMemorySlotCount; ++i)
        m_pMemory[i] = nullptr;

    m_changedFlags.Clear();

    m_pDelayedUpdateTimer = new QTimer(this);
    connect(m_pDelayedUpdateTimer, &QTimer::timeout, this, &TargetModel::delayedTimer);
}

TargetModel::~TargetModel()
{
    for (int i = 0; i < MemorySlot::kMemorySlotCount; ++i)
        delete m_pMemory[i];

    delete m_pDelayedUpdateTimer;
}

void TargetModel::SetConnected(int connected)
{
    m_bConnected = connected;

    if (connected == 0)
    {
        // Clear out lots of data from the model
        m_symbolTable.Reset();

        Breakpoints dummyBreak;
        SetBreakpoints(dummyBreak, 0);
    }

    emit connectChangedSignal();
}

void TargetModel::SetStatus(bool running, uint32_t pc)
{
    m_bRunning = running;
	m_pc = pc;
    m_changedFlags.SetChanged(TargetChangedFlags::kPC);
    emit startStopChangedSignal();

    m_pDelayedUpdateTimer->stop();

    //    if (!m_bRunning)
    {
        m_pDelayedUpdateTimer->setSingleShot(true);
        m_pDelayedUpdateTimer->start(500);
    }
}

void TargetModel::SetConfig(uint32_t machineType, uint32_t cpuLevel)
{
    m_machineType = (MACHINETYPE) machineType;
    m_cpuLevel = cpuLevel;
}

void TargetModel::SetRegisters(const Registers& regs, uint64_t commandId)
{
	m_regs = regs;
    m_changedFlags.SetChanged(TargetChangedFlags::kRegs);
    emit registersChangedSignal(commandId);
}

void TargetModel::SetMemory(MemorySlot slot, const Memory* pMem, uint64_t commandId)
{
    if (m_pMemory[slot])
        delete m_pMemory[slot];

    m_pMemory[slot] = pMem;
    m_changedFlags.SetMemoryChanged(slot);
    emit memoryChangedSignal(slot, commandId);
}

void TargetModel::SetBreakpoints(const Breakpoints& bps, uint64_t commandId)
{
    m_breakpoints = bps;
    m_changedFlags.SetChanged(TargetChangedFlags::kBreakpoints);
    emit breakpointsChangedSignal(commandId);
}

void TargetModel::SetSymbolTable(const SymbolSubTable& syms, uint64_t commandId)
{
    m_symbolTable.SetHatariSubTable(syms);
    m_changedFlags.SetChanged(TargetChangedFlags::kSymbolTable);
    emit symbolTableChangedSignal(commandId);
}

void TargetModel::SetExceptionMask(const ExceptionMask &mask)
{
    m_exceptionMask = mask;
    m_changedFlags.SetChanged(TargetChangedFlags::kExceptionMask);
    emit exceptionMaskChanged();
}

void TargetModel::NotifyMemoryChanged(uint32_t address, uint32_t size)
{
    m_changedFlags.SetChanged(TargetChangedFlags::kOtherMemory);
    emit otherMemoryChanged(address, size);
}


// User-added console command. Anything can happen, so tell everything
// to update
void TargetModel::ConsoleCommand()
{
    emit otherMemoryChanged(0, 0xffffff);
    emit breakpointsChangedSignal(0);
    emit symbolTableChangedSignal(0);
    emit exceptionMaskChanged();
}

void TargetModel::Flush()
{
    emit changedFlush(m_changedFlags);
    m_changedFlags.Clear();
}

void TargetModel::delayedTimer()
{
    m_pDelayedUpdateTimer->stop();
    emit startStopChangedSignalDelayed(m_bRunning);
}

bool IsMachineST(MACHINETYPE type)
{
    return (type == MACHINE_ST || type == MACHINE_MEGA_ST);
}

