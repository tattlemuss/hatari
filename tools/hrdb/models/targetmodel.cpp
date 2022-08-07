#include "targetmodel.h"
#include <iostream>
#include <QTimer>

#include "profiledata.h"

YmState::YmState()
{
    Clear();
}

void YmState::Clear()
{
    for (int i = 0; i < kNumRegs; ++i)
        m_regs[i] = 0;
}

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
    m_bRunning(true),
    m_bProfileEnabled(0),
    m_ffwd(false)
{
    for (int i = 0; i < MemorySlot::kMemorySlotCount; ++i)
        m_pMemory[i] = nullptr;

    m_changedFlags.Clear();
    m_pProfileData = new ProfileData();

    m_pDelayedUpdateTimer = new QTimer(this);
    m_pRunningRefreshTimer = new QTimer(this);

    connect(m_pDelayedUpdateTimer,  &QTimer::timeout, this, &TargetModel::delayedTimer);
    connect(m_pRunningRefreshTimer, &QTimer::timeout, this, &TargetModel::runningRefreshTimerSignal);
}

TargetModel::~TargetModel()
{
    for (int i = 0; i < MemorySlot::kMemorySlotCount; ++i)
        delete m_pMemory[i];

    delete m_pProfileData;
    delete m_pDelayedUpdateTimer;
    delete m_pRunningRefreshTimer;
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

        // Clear potentially running timers
        m_pDelayedUpdateTimer->stop();
        m_pRunningRefreshTimer->stop();
    }

    emit connectChangedSignal();
}

void TargetModel::SetStatus(bool running, uint32_t pc, bool ffwd)
{
    m_bRunning = running;
    m_startStopPc = pc;
    m_ffwd = ffwd;
    m_changedFlags.SetChanged(TargetChangedFlags::kPC);
    emit startStopChangedSignal();      // This should really be statusChanged or something

    m_pDelayedUpdateTimer->stop();
    m_pRunningRefreshTimer->stop();

    // The delay timer always fires
    m_pDelayedUpdateTimer->setSingleShot(true);
    m_pDelayedUpdateTimer->start(500);

    if (m_bRunning)
    {
        m_pRunningRefreshTimer->setSingleShot(false);
        m_pRunningRefreshTimer->start(1000);
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

void TargetModel::SetYm(const YmState& state)
{
    m_ymState = state;
    emit ymChangedSignal();
}

void TargetModel::NotifyMemoryChanged(uint32_t address, uint32_t size)
{
    m_changedFlags.SetChanged(TargetChangedFlags::kOtherMemory);
    emit otherMemoryChangedSignal(address, size);
}

void TargetModel::AddProfileDelta(const ProfileDelta& delta)
{
    m_pProfileData->Add(delta);
}

void TargetModel::ProfileDeltaComplete(int enabled)
{
    m_bProfileEnabled = enabled;
    emit profileChangedSignal();
}

void TargetModel::ProfileReset()
{
    m_pProfileData->Reset();
    emit profileChangedSignal();
}

// User-added console command. Anything can happen, so tell everything
// to update
void TargetModel::ConsoleCommand()
{
    emit otherMemoryChangedSignal(0, 0xffffff);
    emit breakpointsChangedSignal(0);
    emit symbolTableChangedSignal(0);
    emit exceptionMaskChanged();
}

void TargetModel::Flush(uint64_t commmandId)
{
    emit flushSignal(m_changedFlags, commmandId);
    m_changedFlags.Clear();
}

void TargetModel::GetProfileData(uint32_t addr, uint32_t& count, uint32_t& cycles) const
{
    m_pProfileData->Get(addr, count, cycles);
}

const ProfileData& TargetModel::GetRawProfileData() const
{
    return *m_pProfileData;
}

void TargetModel::delayedTimer()
{
    m_pDelayedUpdateTimer->stop();
    emit startStopChangedSignalDelayed(m_bRunning);
}

void TargetModel::runningRefreshTimerSlot()
{
    emit runningRefreshTimerSignal();
}

bool IsMachineST(MACHINETYPE type)
{
    return (type == MACHINE_ST || type == MACHINE_MEGA_ST);
}

bool IsMachineSTE(MACHINETYPE type)
{
    return (type == MACHINE_STE || type == MACHINE_MEGA_STE);
}
