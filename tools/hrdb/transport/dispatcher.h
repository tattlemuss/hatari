#ifndef DISPATCHER_H
#define DISPATCHER_H

#include <string>
#include <deque>
#include "remotecommand.h"
#include "../models/processor.h"
#include <QObject>

class QTcpSocket;
class TargetModel;
class StringSplitter;

// Keeps track of messages between target and host, and matches up commands to responses,
// then passes them to the model.
class Dispatcher : public QObject
{
public:
    Dispatcher(QTcpSocket* tcpSocket, TargetModel* pTargetModel);
    virtual ~Dispatcher() override;

    uint64_t InsertFlush();

    // Request a specific CPU memory block.
    // Sizes are in bytes.
    uint64_t ReadMemory(MemorySlot slot, uint32_t address, uint32_t size);

    // Request a memory block from any memory space.
    // Sizes are NUMBER OF MEMORY LOCATIONS,
    // so bytes for CPU, and DSP-words for DSP
    uint64_t ReadMemory(MemorySlot slot, MemSpace space, uint32_t address, uint32_t size);

    uint64_t ReadRegisters();
    uint64_t ReadInfoYm();
    uint64_t ReadBreakpoints();
    uint64_t ReadExceptionMask();
    uint64_t ReadSymbols();

    uint64_t WriteMemory(uint32_t address, const QVector<uint8_t>& data);

    // System control
    uint64_t ResetWarm();
    uint64_t ResetCold();

    // CPU control
    uint64_t Break();
    uint64_t Run();
    uint64_t Step(Processor proc);
    uint64_t RunToPC(Processor proc, uint32_t pc);

    enum BreakpointFlags
    {
        kBpFlagNone = 0,

        kBpFlagOnce = 1 << 0,
        kBpFlagTrace = 1 << 1
    };

    uint64_t SetBreakpoint(Processor proc, std::string expression, uint64_t optionFlags);
    uint64_t DeleteBreakpoint(Processor proc, uint32_t breakpointId);
    uint64_t SetRegister(Processor proc, int reg, uint32_t val);

    uint64_t SetExceptionMask(uint32_t mask);
    uint64_t SetLoggingFile(const std::string& filename);
    uint64_t SetProfileEnable(bool enable);
    uint64_t SetFastForward(bool enable);
    uint64_t SendConsoleCommand(const std::string& cmd);
    uint64_t SendMemFind(const QVector<uint8_t>& valuesAndMasks, uint32_t startAddress, uint32_t endAddress);
    uint64_t SendSaveBin(uint32_t startAddress, uint32_t size, const std::string& filename);

    // Don't use this except for testing
    uint64_t DebugSendRawPacket(const char* command);

private slots:

   void connected();
   void disconnected();

   // Called by the socket class to process incoming messages
   void readyRead();

private:
    uint64_t SendCommandPacket(const char* command);
    uint64_t SendCommandShared(MemorySlot slot, std::string command);

    void ReceiveResponsePacket(const RemoteCommand& command);
    void ReceiveNotification(const RemoteNotification& notification);
    void ReceivePacket(const char* response);

    void DeletePending();

    // Response parsers for each command
    void ParseRegs(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseMem(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseDmem(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseBplist(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseSymlist(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseExmask(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseMemset(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseInfoym(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseProfile(StringSplitter& splitResp, const RemoteCommand& cmd);
    void ParseMemfind(StringSplitter& splitResp, const RemoteCommand& cmd);

    std::deque<RemoteCommand*>      m_sentCommands;
    QTcpSocket*                     m_pTcpSocket;
    TargetModel*                    m_pTargetModel;

    std::string                     m_active_resp;
    uint64_t                        m_responseUid;

    /* If true, drop incoming packets since they are assumed to be
     * from a previous connection. */
    bool                            m_portConnected;
    bool                            m_waitingConnectionAck;
};

#endif // DISPATCHER_H
