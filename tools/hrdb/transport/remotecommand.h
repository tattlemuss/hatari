#ifndef REMOTECOMMAND_H
#define REMOTECOMMAND_H

#include <string>
#include "../models/memory.h"

class RemoteCommand
{
public:
	std::string		m_cmd;
	std::string		m_response;		// filled out by receiver thread
    MemorySlot      m_memorySlot;   // what this command is associated with
    uint64_t        m_uid;          // Tracking UID updated by dispatcher
};

class RemoteNotification
{
public:
	std::string		m_payload;
};


#endif // REMOTECOMMAND_H
