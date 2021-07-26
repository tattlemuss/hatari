#include "targetmodel.h"

#include <iostream>
#include <QTimer>

//-----------------------------------------------------------------------------
const char* Registers::s_names[] =
{
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
	"PC", "SR", 
	"USP", "ISP",
	"EX",
    // Vars
    "AesOpcode",
    "Basepage",
    "BiosOpcode",
    "BSS",
    "CpuInstr",
    "CpuOpcodeType",
    "CycleCounter",
    "DATA",
    "DspInstr",
    "DspOpcodeType",
    "FrameCycles",
    "GemdosOpcode",
    "HBL",
    "LineAOpcode",
    "LineCycles",
    "LineFOpcode",
    "NextPC",
    "OsCallParam",
    "TEXT",
    "TEXTEnd",
    "VBL",
    "VdiOpcode",
    "XbiosOpcode",
    nullptr
};

Registers::Registers()
{
	for (int i = 0; i < Registers::REG_COUNT; ++i)
		m_value[i] = 0;
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
    m_bRunning(true)
{
    for (int i = 0; i < MemorySlot::kMemorySlotCount; ++i)
        m_pTestMemory[i] = nullptr;

    m_changedFlags.Clear();

    m_pTimer = new QTimer(this);
    connect(m_pTimer, &QTimer::timeout, this, &TargetModel::delayedTimer);
}

TargetModel::~TargetModel()
{
    for (int i = 0; i < MemorySlot::kMemorySlotCount; ++i)
        delete m_pTestMemory[i];

    delete m_pTimer;
}

void TargetModel::SetConnected(int connected)
{
    m_bConnected = connected;

    if (connected == 0)
    {
        // Clear out lots of data from the model
        SymbolTable dummy;
        SetSymbolTable(dummy, 0);

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

    m_pTimer->stop();

    //    if (!m_bRunning)
    {
        m_pTimer->setSingleShot(true);
        m_pTimer->start(500);
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
    if (m_pTestMemory[slot])
        delete m_pTestMemory[slot];

    m_pTestMemory[slot] = pMem;
    m_changedFlags.SetMemoryChanged(slot);
    emit memoryChangedSignal(slot, commandId);
}

void TargetModel::SetBreakpoints(const Breakpoints& bps, uint64_t commandId)
{
    m_breakpoints = bps;
    m_changedFlags.SetChanged(TargetChangedFlags::kBreakpoints);
    emit breakpointsChangedSignal(commandId);
}

void TargetModel::SetSymbolTable(const SymbolTable& syms, uint64_t commandId)
{
    m_symbolTable = syms;
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

void TargetModel::Flush()
{
    emit changedFlush(m_changedFlags);
    m_changedFlags.Clear();
}

void TargetModel::delayedTimer()
{
    m_pTimer->stop();
    emit startStopChangedSignalDelayed(m_bRunning);
}
