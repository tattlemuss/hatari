#include "../transport/dispatcher.h"
#include <QtWidgets>
#include <QtNetwork>

#include <iostream>

#include "../models/targetmodel.h"
#include "../models/stringsplitter.h"
#include "../models/stringparsers.h"
#include "../models/profiledata.h"

//#define DISPATCHER_DEBUG

// Protocol ID which needs to match the Hatari target
#define REMOTEDEBUG_PROTOCOL_ID	(0x1008)

//-----------------------------------------------------------------------------
// Character value for the separator in responses/notifications from the target
static const char SEP_CHAR = 1;

//-----------------------------------------------------------------------------
int RegNameToEnum(const char* name)
{
    const char** pCurrName = Registers::s_names;
    int i = 0;
    while (*pCurrName)
    {
        if (strcmp(name, *pCurrName) == 0)
            return i;
        ++pCurrName;
        ++i;
    }
    return Registers::REG_COUNT;
}

int DspRegNameToEnum(const char* name)
{
    const char** pCurrName = DspRegisters::s_names;
    int i = 0;
    while (*pCurrName)
    {
        if (strcmp(name, *pCurrName) == 0)
            return i;
        ++pCurrName;
        ++i;
    }
    return DspRegisters::REG_COUNT;
}

//-----------------------------------------------------------------------------
Dispatcher::Dispatcher(QTcpSocket* tcpSocket, TargetModel* pTargetModel) :
    m_pTcpSocket(tcpSocket),
    m_pTargetModel(pTargetModel),
    m_responseUid(100),
    m_portConnected(false),
    m_waitingConnectionAck(false)
{
    connect(m_pTcpSocket, &QAbstractSocket::connected,    this, &Dispatcher::connected);
    connect(m_pTcpSocket, &QAbstractSocket::disconnected, this, &Dispatcher::disconnected);
    connect(m_pTcpSocket, &QAbstractSocket::readyRead,    this, &Dispatcher::readyRead);
}

Dispatcher::~Dispatcher()
{
    DeletePending();
}

uint64_t Dispatcher::InsertFlush()
{
    Q_ASSERT(m_portConnected && !m_waitingConnectionAck);
    RemoteCommand* pNewCmd = new RemoteCommand();
    pNewCmd->m_cmd = "flush";
    pNewCmd->m_memorySlot = MemorySlot::kNone;
    pNewCmd->m_uid = m_responseUid++;
    m_sentCommands.push_front(pNewCmd);
    // Don't send it down the wire!
    return pNewCmd->m_uid;
}

uint64_t Dispatcher::ReadMemory(MemorySlot slot, uint32_t address, uint32_t size)
{
    QString tmp = QString::asprintf("mem %x %x", address, size);
    return SendCommandShared(slot, tmp.toStdString());
}

uint64_t Dispatcher::ReadDspMemory(MemorySlot slot, char space, uint32_t address, uint32_t size)
{
    QString tmp = QString::asprintf("dmem %c %x %x", space, address, size);
    return SendCommandShared(slot, tmp.toStdString());
}

uint64_t Dispatcher::ReadInfoYm()
{
    return SendCommandPacket("infoym");
}

uint64_t Dispatcher::ReadBreakpoints()
{
    return SendCommandPacket("bplist");
}

uint64_t Dispatcher::ReadExceptionMask()
{
    return SendCommandPacket("exmask");
}

uint64_t Dispatcher::ReadSymbols()
{
    return SendCommandPacket("symlist");
}

uint64_t Dispatcher::WriteMemory(uint32_t address, const QVector<uint8_t> &data)
{
    QString command = QString::asprintf("memset %x %x ", address, data.size());
    for (int i = 0; i <  data.size(); ++i)
        command += QString::asprintf("%02x", data[i]);

    return SendCommandPacket(command.toStdString().c_str());
}

uint64_t Dispatcher::ResetWarm()
{
    return SendCommandPacket("resetwarm");
}

uint64_t Dispatcher::ResetCold()
{
    return SendCommandPacket("resetcold");
}

uint64_t Dispatcher::Break()
{
    return SendCommandPacket("break");
}

uint64_t Dispatcher::Run()
{
    return SendCommandPacket("run");
}

uint64_t Dispatcher::Step(Processor proc)
{
    if (proc == kProcCpu)
        return SendCommandPacket("step");
    else
        return SendCommandPacket("dstep");
}

uint64_t Dispatcher::ReadRegisters()
{
    return SendCommandPacket("regs");
}

uint64_t Dispatcher::RunToPC(uint32_t next_pc)
{
    QString str = QString::asprintf("bp pc = $%x : once", next_pc);
    SendCommandPacket(str.toStdString().c_str());
    return SendCommandPacket("run");
}

uint64_t Dispatcher::SetBreakpoint(std::string expression, uint64_t optionFlags)
{
    std::string command = std::string("bp " + expression);

    // Add extra options
    if (optionFlags & kBpFlagOnce)
        command += ": once";
    if (optionFlags & kBpFlagTrace)
        command += ": trace";
    SendCommandShared(MemorySlot::kNone, command);
    return SendCommandShared(MemorySlot::kNone, "bplist"); // update state
}

uint64_t Dispatcher::DeleteBreakpoint(uint32_t breakpointId)
{
    QString cmd = QString::asprintf("bpdel %x", breakpointId);
    SendCommandPacket(cmd.toStdString().c_str());
    return SendCommandPacket("bplist");
}


uint64_t Dispatcher::SetRegister(int reg, uint32_t val)
{
    const char* pRegName = Registers::s_names[reg];
    QString cmd = QString::asprintf("console r %s=$%x", pRegName, val);
    return SendCommandPacket(cmd.toStdString().c_str());
}

uint64_t Dispatcher::SetExceptionMask(uint32_t mask)
{
    return SendCommandPacket(QString::asprintf("exmask %x", mask).toStdString().c_str());
}

uint64_t Dispatcher::SetLoggingFile(const std::string& filename)
{
    // Tell the target to redirect
    std::string cmd("setstd ");
    cmd += filename;
    return SendCommandPacket(cmd.c_str());
}

uint64_t Dispatcher::SetProfileEnable(bool enable)
{
    if (enable)
        return SendCommandPacket("profile 1");
    else
        return SendCommandPacket("profile 0");
}

uint64_t Dispatcher::SetFastForward(bool enable)
{
    if (enable)
        return SendCommandPacket("ffwd 1");
    else
        return SendCommandPacket("ffwd 0");
}

uint64_t Dispatcher::SendConsoleCommand(const std::string& cmd)
{
    std::string packet = "console ";
    packet += cmd;
    return SendCommandPacket(packet.c_str());
}

uint64_t Dispatcher::SendMemFind(const QVector<uint8_t>& valuesAndMasks, uint32_t startAddress, uint32_t endAddress)
{
    QString command = QString::asprintf("memfind %x %x ", startAddress, endAddress - startAddress);

    for (int i = 0; i <  valuesAndMasks.size(); ++i)
        command += QString::asprintf("%02x", valuesAndMasks[i]);

    return SendCommandPacket(command.toStdString().c_str());
}

uint64_t Dispatcher::SendSaveBin(uint32_t startAddress, uint32_t size, const std::string &filename)
{
    QString command = QString::asprintf("savebin %x %x %s", startAddress, size,
                                        filename.c_str());
    return SendCommandPacket(command.toStdString().c_str());
}

uint64_t Dispatcher::DebugSendRawPacket(const char *command)
{
    return SendCommandPacket(command);
}

void Dispatcher::ReceivePacket(const char* response)
{
    // THIS HAPPENS ON THE EVENT LOOP
    std::string new_resp(response);

    // Any flushes to handle?
    while (1)
    {
        if (m_sentCommands.size() == 0)
            break;
        if (m_sentCommands.back()->m_cmd != "flush")
            break;

        uint64_t commandId = m_sentCommands.back()->m_uid;
        delete m_sentCommands.back();
        m_sentCommands.pop_back();
        m_pTargetModel->Flush(commandId);
    }

    // Check for a notification
    if (new_resp.size() > 0)
    {
        if (new_resp[0] == '!')
        {
            RemoteNotification notif;
            notif.m_payload = new_resp;
            this->ReceiveNotification(notif);
            return;
        }
    }

    // Handle replies to normal commands.
    // If we have just connected, new packets might be from the old connection,
    // so ditch them
    if (m_waitingConnectionAck)
    {
        std::cout << "Dropping old response" << new_resp << std::endl;
        return;
    }

    // Find the last "sent" packet
    size_t checkIndex = m_sentCommands.size();
    if (checkIndex != 0)
    {
        // Pair to the last entry
        RemoteCommand* pPending = m_sentCommands[checkIndex - 1];
        pPending->m_response = new_resp;

        m_sentCommands.pop_back();

        // Add these to a list and process them at a safe time?
        // Need to understand the Qt threading mechanism
        //std::cout << "Cmd: " << pPending->m_cmd << ", Response: " << pPending->m_response << "***" << std::endl;

        // At this point we can notify others that new data has arrived
        this->ReceiveResponsePacket(*pPending);
        delete pPending;

        // Any flushes to handle?
        while (1)
        {
            if (m_sentCommands.size() == 0)
                break;
            if (m_sentCommands.back()->m_cmd != "flush")
                break;

            uint64_t commandId = m_sentCommands.back()->m_uid;
            delete m_sentCommands.back();
            m_sentCommands.pop_back();
            m_pTargetModel->Flush(commandId);
        }
    }
    else
    {
        std::cout << "No pending command??" << std::endl;
    }

}

void Dispatcher::DeletePending()
{
    std::deque<RemoteCommand*>::iterator it = m_sentCommands.begin();
    while (it != m_sentCommands.end())
    {
        delete *it;
        ++it;
    }
    m_sentCommands.clear();
}

void Dispatcher::connected()
{
    // Flag that we are awaiting the "connected" notification
    m_waitingConnectionAck = true;

    // Clear any accidental button clicks that sent messages while disconnected
    DeletePending();

    m_portConnected = true;

    // THIS HAPPENS ON THE EVENT LOOP
    std::cout << "Host connected, awaiting ack" << std::endl;
}

void Dispatcher::disconnected()
{
    m_pTargetModel->SetConnected(0);

    // Clear pending commands so that incoming responses are not confused with the first connection
    DeletePending();

    // THIS HAPPENS ON THE EVENT LOOP
    std::cout << "Host disconnected" << std::endl;
    m_portConnected = false;
}

void Dispatcher::readyRead()
{
    // THIS HAPPENS ON THE EVENT LOOP
    qint64 byteCount = m_pTcpSocket->bytesAvailable();
    char* data = new char[byteCount];

    m_pTcpSocket->read(data, byteCount);

    // Read completed commands from this and process in turn
    for (int i = 0; i < byteCount; ++i)
    {
        if (data[i] == 0)
        {
            // End of response
            this->ReceivePacket(m_active_resp.c_str());
            m_active_resp = std::string();
        }
        else
        {
            m_active_resp += data[i];
        }
    }
    delete[] data;
}

uint64_t Dispatcher::SendCommandPacket(const char *command)
{
    return SendCommandShared(MemorySlot::kNone, command);
}

uint64_t Dispatcher::SendCommandShared(MemorySlot slot, std::string command)
{
    Q_ASSERT(m_portConnected && !m_waitingConnectionAck);
    if (!m_portConnected || m_waitingConnectionAck)
    {
        std::cerr << "WARNING: ditching command \"" << command << "\" since not connected" << std::endl;
        return 0ULL;
    }

    RemoteCommand* pNewCmd = new RemoteCommand();
    pNewCmd->m_cmd = command;
    pNewCmd->m_memorySlot = slot;
    pNewCmd->m_uid = m_responseUid++;
    m_sentCommands.push_front(pNewCmd);
    m_pTcpSocket->write(command.c_str(), command.size() + 1);
#ifdef DISPATCHER_DEBUG
    std::cout << "COMMAND:" << pNewCmd->m_cmd << std::endl;
#endif
    return pNewCmd->m_uid;
}

void Dispatcher::ReceiveResponsePacket(const RemoteCommand& cmd)
{
#ifdef DISPATCHER_DEBUG
    std::cout << "REPONSE:" << cmd.m_cmd << "//" << cmd.m_response << std::endl;
#endif

    // Our handling depends on the original command type
    // e.g. "break"
    StringSplitter splitCmd(cmd.m_cmd);
    std::string type = splitCmd.Split(' '); // commands use space for separators
    StringSplitter splitResp(cmd.m_response);
    std::string cmd_status = splitResp.Split(SEP_CHAR);
    if (cmd_status != std::string("OK"))
    {
        assert(cmd_status == "NG");
        std::cout << "WARNING: Repsonse dropped: " << cmd.m_response << std::endl;
        std::cout << "WARNING: Original command: " << cmd.m_cmd << std::endl;

        // "NG" commands return a value now, so parse that
        // for future information
        std::string valueStr = splitResp.Split(SEP_CHAR);
        uint32_t value;
        if (!StringParsers::ParseHexString(valueStr.c_str(), value))
            return;

        // Send signals for some packet types
        if (type == "savebin")
            m_pTargetModel->saveBinCompleteSignal(cmd.m_uid, value);

        return;
    }

    // Handle responses with arguments first, and pass to a parser
    if (type == "regs")
        ParseRegs(splitResp, cmd);
    else if (type == "mem")
        ParseMem(splitResp, cmd);
    else if (type == "dmem")
        ParseDmem(splitResp, cmd);
    else if (type == "bplist")
        ParseBplist(splitResp, cmd);
    else if (type == "symlist")
        ParseSymlist(splitResp, cmd);
    else if (type == "exmask")
        ParseExmask(splitResp, cmd);
    else if (type == "memset")
        ParseMemset(splitResp, cmd);
    else if (type == "infoym")
        ParseInfoym(splitResp, cmd);
    else if (type == "profile")
        ParseProfile(splitResp, cmd);
    else if (type == "memfind")
        ParseMemfind(splitResp, cmd);
    else if (type == "resetwarm")
    {
        // Nothing to do now, since the !symbol notifier will clean up for us
    }
    else if (type == "flush")
    {
        Q_ASSERT(0);
    }
    else if (type == "console")
        // Anything could have happened here!
        m_pTargetModel->ConsoleCommand();
    else if (type == "savebin")
       m_pTargetModel->SaveBinComplete(cmd.m_uid, 0U);
    else
    {
        // For debugging
        //std::cout << "UNKNOWN REPONSE:" << cmd.m_cmd << "//" << cmd.m_response << std::endl;
    }
}

void Dispatcher::ReceiveNotification(const RemoteNotification& cmd)
{
#ifdef DISPATCHER_DEBUG
    std::cout << "NOTIFICATION:" << cmd.m_payload << std::endl;
#endif
    StringSplitter s(cmd.m_payload);
    std::string type = s.Split(SEP_CHAR);

    // Only accept one message type if we are awaiting a protcol ack
    if (m_waitingConnectionAck)
    {
        if (type == "!connected")
        {
            // Check the protocol
            std::string protoColIdStr = s.Split(SEP_CHAR);
            uint32_t protocolId;
            if (!StringParsers::ParseHexString(protoColIdStr.c_str(), protocolId))
                return;

            if (protocolId != REMOTEDEBUG_PROTOCOL_ID)
            {
                std::cout << "Connection refused (wrong protocol)" << std::endl;
                m_pTcpSocket->disconnectFromHost();
                disconnected(); // do our cleanup too
                m_waitingConnectionAck = false;
                m_pTargetModel->SetProtocolMismatch(protocolId, REMOTEDEBUG_PROTOCOL_ID);
                return;
            }
            // This connection looks ok
            // Allow new command responses to be processed.
            m_waitingConnectionAck = false;

            std::cout << "Connection acknowleged by server" << std::endl;
            m_pTargetModel->SetConnected(1);
        }
        return;
    }

    // Ditch any notifications that might still be coming through after a
    // connection rejection
    if (!m_portConnected)
        return;

    if (type == "!status")
    {
        std::string runningStr = s.Split(SEP_CHAR);
        std::string pcStr = s.Split(SEP_CHAR);
        std::string dspPcStr = s.Split(SEP_CHAR);
        std::string ffwdStr = s.Split(SEP_CHAR);
        uint32_t running;
        uint32_t pc;
        uint32_t dsp_pc;
        uint32_t ffwd;
        if (!StringParsers::ParseHexString(runningStr.c_str(), running))
            return;
        if (!StringParsers::ParseHexString(pcStr.c_str(), pc))
            return;
        if (!StringParsers::ParseHexString(dspPcStr.c_str(), dsp_pc))
            return;
        if (!StringParsers::ParseHexString(ffwdStr.c_str(), ffwd))
            return;

        // This call goes off and lots of views insert requests here, so add a flush into the queue
        m_pTargetModel->SetStatus(running != 0, pc, dsp_pc, ffwd);
        this->InsertFlush();
    }
    else if (type == "!config")
    {
        std::string machineTypeStr = s.Split(SEP_CHAR);
        std::string cpuLevelStr = s.Split(SEP_CHAR);
        std::string stRamSizeStr = s.Split(SEP_CHAR);
        std::string dspActiveStr = s.Split(SEP_CHAR);
        uint32_t machineType;
        uint32_t cpuLevel;
        uint32_t stRamSize;
        uint32_t dspActive;
        if (!StringParsers::ParseHexString(machineTypeStr.c_str(), machineType))
            return;
        if (!StringParsers::ParseHexString(cpuLevelStr.c_str(), cpuLevel))
            return;
        if (!StringParsers::ParseHexString(stRamSizeStr.c_str(), stRamSize))
            return;
        if (!StringParsers::ParseHexString(dspActiveStr.c_str(), dspActive))
            return;
        m_pTargetModel->SetConfig(machineType, cpuLevel, stRamSize, dspActive);
        this->InsertFlush();
    }
    else if (type == "!profile")
    {
        uint32_t enabled = 0;
        std::string enabledStr = s.Split(SEP_CHAR);
        if (!StringParsers::ParseHexString(enabledStr.c_str(), enabled))
            return;

        uint32_t lastaddr = 0;
        int numDeltas = 0;
        while (true)
        {
            std::string addrDeltaStr = s.Split(SEP_CHAR);
            if (addrDeltaStr.size() == 0)
                break;
            std::string countStr = s.Split(SEP_CHAR);
            std::string cyclesStr = s.Split(SEP_CHAR);

            uint32_t addrDelta = 0;
            uint32_t count = 0;
            uint32_t cycles = 0;

            if (!StringParsers::ParseHexString(addrDeltaStr.c_str(), addrDelta))
                return;
            if (!StringParsers::ParseHexString(countStr.c_str(), count))
                return;
            if (!StringParsers::ParseHexString(cyclesStr.c_str(), cycles))
                return;

            uint32_t newaddr = lastaddr + addrDelta;
            ProfileDelta delta;
            delta.addr = newaddr;
            delta.count = count;
            delta.cycles = cycles;
            m_pTargetModel->AddProfileDelta(delta);
            lastaddr = newaddr;
            ++numDeltas;
        }
        m_pTargetModel->ProfileDeltaComplete(static_cast<int>(enabled));
    }  
    else if (type == "!symbols")
    {
        std::string path = s.Split(SEP_CHAR);
        std::cout << "New program for symbol table: '" << path << "'" << std::endl;
        m_pTargetModel->NotifySymbolProgramChanged();
    }
}

void Dispatcher::ParseRegs(StringSplitter& splitResp, const RemoteCommand& cmd)
{
    Registers regs;
    DspRegisters dspRegs;
    while (true)
    {
        std::string reg = splitResp.Split(SEP_CHAR);
        if (reg.size() == 0)
            break;
        std::string valueStr = splitResp.Split(SEP_CHAR);
        uint32_t value;
        if (!StringParsers::ParseHexString(valueStr.c_str(), value))
            return;

        // Write this value into register structure
        // NOTE: this is tolerant to not matching the name
        // since we use "Vars"
        if (reg.find("D_", 0) == 0)
        {
            int reg_id = DspRegNameToEnum(reg.c_str() + 2);
            if (reg_id != DspRegisters::REG_COUNT)
                dspRegs.Set(reg_id, value);
        }
        else
        {
            int reg_id = RegNameToEnum(reg.c_str());
            if (reg_id != Registers::REG_COUNT)
                regs.m_value[reg_id] = value;
        }
    }
    m_pTargetModel->SetRegisters(regs, dspRegs, cmd.m_uid);
}

void Dispatcher::ParseMem(StringSplitter& splitResp, const RemoteCommand& cmd)
{
    std::string addrStr = splitResp.Split(SEP_CHAR);
    std::string sizeStr = splitResp.Split(SEP_CHAR);
    uint32_t addr;
    if (!StringParsers::ParseHexString(addrStr.c_str(), addr))
        return;
    uint32_t size;
    if (!StringParsers::ParseHexString(sizeStr.c_str(), size))
        return;

    // Create a new memory block to pass to the data model
    Memory* pMem = new Memory(Memory::kCpu, addr, size);

    // Now parse the uuencoded data
    // Each "group" encodes 3 bytes
    uint32_t numGroups = (size + 2) / 3;        // round up to next block

    uint32_t writePos = 0;
    for (uint32_t group = 0; group < numGroups; ++group)
    {
        uint32_t accum = 0;
        for (int i = 0; i < 4; ++i)
        {
            accum <<= 6;
            uint32_t value = splitResp.GetNext();
            assert(value >= 32 && value < 32+64);
            accum |= (value - 32u);
        }

        // Now output 3 chars
        for (int i = 0; i < 3; ++i)
        {
            if (writePos == size)
                break;
            pMem->Set(writePos++, (accum >> 16) & 0xff);
            accum <<= 8;
        }
    }
    m_pTargetModel->SetMemory(cmd.m_memorySlot, pMem, cmd.m_uid);
}

void Dispatcher::ParseDmem(StringSplitter &splitResp, const RemoteCommand &cmd)
{
    std::string memspace = splitResp.Split(SEP_CHAR);
    std::string addrStr = splitResp.Split(SEP_CHAR);
    std::string sizeStr = splitResp.Split(SEP_CHAR);
    uint32_t addr;
    if (!StringParsers::ParseHexString(addrStr.c_str(), addr))
        return;
    uint32_t sizeInWords;
    if (!StringParsers::ParseHexString(sizeStr.c_str(), sizeInWords))
        return;
    Memory::Space space;
    switch (memspace[0])
    {
        case 'P' : space = Memory::kDspP; break;
        case 'X' : space = Memory::kDspX; break;
        case 'Y' : space = Memory::kDspY; break;
    default:
        return;
    }

    // Create a new memory block to pass to the data model
    Memory* pMem = new Memory(space, addr, sizeInWords * 3);

    // Now parse the uuencoded data
    // Each "group" encodes 3 bytes
    uint32_t numGroups = sizeInWords;        // round up to next block

    uint32_t writePos = 0;
    uint32_t readPos = splitResp.GetPos();
    for (uint32_t group = 0; group < numGroups; ++group)
    {
        uint32_t accum = 0;
        for (int i = 0; i < 4; ++i)
        {
            accum <<= 6;
            uint32_t value = cmd.m_response[readPos++];
            assert(value >= 32 && value < 32+64);
            accum |= (value - 32u);
        }

        // Now output 3 chars
        for (int i = 0; i < 3; ++i)
        {
            pMem->Set(writePos++, (accum >> 16) & 0xff);
            accum <<= 8;
        }
    }

    m_pTargetModel->SetMemory(cmd.m_memorySlot, pMem, cmd.m_uid);
}

void Dispatcher::ParseBplist(StringSplitter& splitResp, const RemoteCommand& cmd)
{
    // Breakpoints
    std::string countStr = splitResp.Split(SEP_CHAR);
    uint32_t count;
    if (!StringParsers::ParseHexString(countStr.c_str(), count))
        return;

    Breakpoints bps;
    for (uint32_t i = 0; i < count; ++i)
    {
        Breakpoint bp;
        bp.m_id = i + 1;        // IDs in Hatari start at 1 :(
        bp.SetExpression(splitResp.Split(SEP_CHAR));
        std::string ccountStr = splitResp.Split(SEP_CHAR);
        std::string hitsStr = splitResp.Split(SEP_CHAR);
        std::string onceStr = splitResp.Split(SEP_CHAR);
        std::string quietStr = splitResp.Split(SEP_CHAR);
        std::string traceStr = splitResp.Split(SEP_CHAR);
        if (!StringParsers::ParseHexString(ccountStr.c_str(), bp.m_conditionCount))
            return;
        if (!StringParsers::ParseHexString(hitsStr.c_str(), bp.m_hitCount))
            return;
        if (!StringParsers::ParseHexString(onceStr.c_str(), bp.m_once))
            return;
        if (!StringParsers::ParseHexString(quietStr.c_str(), bp.m_quiet))
            return;
        if (!StringParsers::ParseHexString(traceStr.c_str(), bp.m_trace))
            return;
        bps.m_breakpoints.push_back(bp);
    }
    m_pTargetModel->SetBreakpoints(bps, cmd.m_uid);
}

void Dispatcher::ParseSymlist(StringSplitter& splitResp, const RemoteCommand& cmd)
{
    // Symbols
    std::string countStr = splitResp.Split(SEP_CHAR);
    uint32_t count;
    if (!StringParsers::ParseHexString(countStr.c_str(), count))
        return;

    const std::string absType("A");
    SymbolSubTable syms;
    for (uint32_t i = 0; i < count; ++i)
    {
        std::string name = splitResp.Split(SEP_CHAR);
        std::string addrStr = splitResp.Split(SEP_CHAR);
        uint32_t address;
        if (!StringParsers::ParseHexString(addrStr.c_str(), address))
            return;
        std::string type = splitResp.Split(SEP_CHAR);
        if (type == absType)
            continue;
        uint32_t size = 0;
        syms.AddSymbol(name, address, size, type, std::string());
    }
    m_pTargetModel->SetSymbolTable(syms, cmd.m_uid);
}

void Dispatcher::ParseExmask(StringSplitter &splitResp, const RemoteCommand &cmd)
{
    (void)cmd;
    std::string maskStr = splitResp.Split(SEP_CHAR);
    uint32_t mask;
    if (!StringParsers::ParseHexString(maskStr.c_str(), mask))
        return;

    ExceptionMask maskObj;
    maskObj.m_mask = (uint16_t)mask;
    m_pTargetModel->SetExceptionMask(maskObj);
}

void Dispatcher::ParseMemset(StringSplitter &splitResp, const RemoteCommand &cmd)
{
    (void)cmd;
    // check the affected range
    std::string addrStr = splitResp.Split(SEP_CHAR);
    std::string sizeStr = splitResp.Split(SEP_CHAR);
    uint32_t addr;
    if (!StringParsers::ParseHexString(addrStr.c_str(), addr))
        return;
    uint32_t size;
    if (!StringParsers::ParseHexString(sizeStr.c_str(), size))
        return;

    m_pTargetModel->NotifyMemoryChanged(addr, size);
}

void Dispatcher::ParseInfoym(StringSplitter &splitResp, const RemoteCommand &cmd)
{
    (void)cmd;
    YmState state;
    for (int i = 0; i < YmState::kNumRegs; ++i)
    {
        std::string valueStr = splitResp.Split(SEP_CHAR);
        if (valueStr.size() == 0)
            return;
        uint32_t value;
        if (!StringParsers::ParseHexString(valueStr.c_str(), value))
            return;
        state.m_regs[i] = static_cast<uint8_t>(value);
    }
    m_pTargetModel->SetYm(state);
}

void Dispatcher::ParseProfile(StringSplitter &splitResp, const RemoteCommand &cmd)
{
    (void)cmd;
    uint32_t enabled = 0;
    std::string enabledStr = splitResp.Split(SEP_CHAR);
    if (!StringParsers::ParseHexString(enabledStr.c_str(), enabled))
        return;
    m_pTargetModel->ProfileDeltaComplete(static_cast<int>(enabled));
}

void Dispatcher::ParseMemfind(StringSplitter &splitResp, const RemoteCommand &cmd)
{
    SearchResults results;
    while (true)
    {
        std::string addrStr = splitResp.Split(SEP_CHAR);
        if (addrStr.size() == 0)
            break;
        uint32_t value;
        if (!StringParsers::ParseHexString(addrStr.c_str(), value))
            break;
        results.addresses.push_back(value);
    }
    m_pTargetModel->SetSearchResults(cmd.m_uid, results);
}
