/*
 * Hatari - remotedebug.c
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * Remote debugging support via a network port.
 * 
 */

#include "remotedebug.h"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if HAVE_UNIX_DOMAIN_SOCKETS
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#if HAVE_WINSOCK_SOCKETS
#include <winsock.h>
#endif

#include "m68000.h"
#include "main.h"		/* For ARRAY_SIZE, event handler */
#include "debugui.h"	/* For DebugUI_RegisterRemoteDebug */
#include "debugcpu.h"	/* For stepping */
#include "evaluate.h"
#include "stMemory.h"
#include "breakcond.h"
#include "symbols.h"
#include "log.h"
#include "vars.h"
#include "memory.h"
#include "configuration.h"

#define REMOTE_DEBUG_PORT          (56001)
#define REMOTE_DEBUG_CMD_MAX_SIZE  (300)

// How many bytes we collect to send chunks for the "mem" command
#define RDB_MEM_BLOCK_SIZE         (2048)

// Network timeout when in break loop, to allow event handler update.
// Currently 0.2sec
#define RDB_SELECT_TIMEOUT_USEC   (200000)

/* Remote debugging break command was sent from debugger */
static bool bRemoteBreakRequest = false;

/* Processing is stopped and the remote debug loop is active */
static bool bRemoteBreakIsActive = false;

// Transmission functions (wrapped for platform portability)
// -----------------------------------------------------------------------------
static void send_str(int fd, const char* pStr)
{
	send(fd, pStr, strlen(pStr), 0);
}

// -----------------------------------------------------------------------------
static void send_hex(int fd, uint32_t val)
{
	char str[9];
	sprintf(str, "%X", val);
	send(fd, str, strlen(str), 0);
}

// -----------------------------------------------------------------------------
static void send_char(int fd, char val)
{
	send(fd, &val, 1, 0);
}

// -----------------------------------------------------------------------------
static void send_bool(int fd, bool val)
{
	send_char(fd, val ? '1' : '0');
}

// -----------------------------------------------------------------------------
static void send_key_value(int fd, const char* pStr, uint32_t val)
{
	send_str(fd, " ");
	send_str(fd, pStr);
	send_str(fd, ":");
	send_hex(fd, val);
}

// -----------------------------------------------------------------------------
static void send_term(int fd)
{
	char null = 0;
	send(fd, &null, 1, 0);
}

//-----------------------------------------------------------------------------
static bool read_hex_char(char c, uint8_t* result)
{
    if (c >= '0' && c <= '9')
    {
        *result = (uint8_t)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        *result = (uint8_t)(10 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        *result = (uint8_t)(10 + c - 'A');
        return true;
    }
    *result = 0;
    return false;
}

// -----------------------------------------------------------------------------
// Send the out-of-band status to flag start/stop
static int RemoteDebug_NotifyState(int fd)
{
	char tmp[100];
	sprintf(tmp, "!status %x %x", bRemoteBreakIsActive ? 0 : 1, M68000_GetPC());
	send_str(fd, tmp);
	send_term(fd);
	return 0;
}

// -----------------------------------------------------------------------------
static int RemoteDebug_NotifyConfig(int fd)
{
	const CNF_SYSTEM* system = &ConfigureParams.System;
	char tmp[100];
	sprintf(tmp, "!config %x %x", 
		system->nMachineType, system->nCpuLevel);
	
	send_str(fd, tmp);
	send_term(fd);
	return 0;
}

// -----------------------------------------------------------------------------
/* Return short status info in a useful format, mainly whether it's running */
static int RemoteDebug_Status(int nArgc, char *psArgs[], int fd)
{
	char tmp[100];
	int len = sprintf(tmp, "OK %x %x", bRemoteBreakIsActive ? 0 : 1, M68000_GetPC());
	send(fd, tmp, len, 0);
	return 0;
}

// -----------------------------------------------------------------------------
/* Put in a break request which is serviced elsewhere in the main loop */
static int RemoteDebug_Break(int nArgc, char *psArgs[], int fd)
{
	// Only set a break request if we are running
	if (!bRemoteBreakIsActive)
	{
		bRemoteBreakRequest = true;
		send_str(fd, "OK");
	}
	else
	{
		return 1;
	}
	
	return 0;
}

// -----------------------------------------------------------------------------
/* Step next instruction. This is currently a passthrough to the normal debugui code. */
static int RemoteDebug_Step(int nArgc, char *psArgs[], int fd)
{
	DebugCpu_SetSteps(1);
	send_str(fd, "OK");

	// Restart
	bRemoteBreakIsActive = false;
	return 0;
}

// -----------------------------------------------------------------------------
static int RemoteDebug_Run(int nArgc, char *psArgs[], int fd)
{
	send_str(fd, "OK");
	bRemoteBreakIsActive = false;
	return 0;
}

/**
 * Dump register contents. 
 * This also includes Hatari variables, which we treat as a subset of regs.
 * 
 * Input: "regs\n"
 * 
 * Output: "regs <reg:value>*N\n"
 */
static int RemoteDebug_Regs(int nArgc, char *psArgs[], int fd)
{
	int regIdx;
	Uint32 varIndex;
	varIndex = 0;
	const var_addr_t* var;

	static const int regIds[] = {
		REG_D0, REG_D1, REG_D2, REG_D3, REG_D4, REG_D5, REG_D6, REG_D7,
		REG_A0, REG_A1, REG_A2, REG_A3, REG_A4, REG_A5, REG_A6, REG_A7 };
	static const char *regNames[] = {
		"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
		"A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7" };

	send_str(fd, "OK ");

	// Normal regs
	for (regIdx = 0; regIdx < ARRAY_SIZE(regIds); ++regIdx)
		send_key_value(fd, regNames[regIdx], Regs[regIds[regIdx]]);
		
	// Special regs
	send_key_value(fd, "PC", M68000_GetPC());
	send_key_value(fd, "USP", regs.usp);
	send_key_value(fd, "ISP", regs.isp);
	send_key_value(fd, "SR", M68000_GetSR());
	send_key_value(fd, "EX", regs.exception);

	// Variables
	while (Vars_QueryVariable(varIndex, &var))
	{
		Uint32 value;
		value = Vars_GetValue(var);
		send_key_value(fd, var->name, value);
		++varIndex;
	}

	return 0;
}

/**
 * Dump the requested area of ST memory.
 *
 * Input: "mem <start addr> <size in bytes>\n"
 *
 * Output: "mem <address-expr> <size-expr> <memory as base16 string>\n"
 */

static int RemoteDebug_Mem(int nArgc, char *psArgs[], int fd)
{
	int arg;
	Uint8 value;
	Uint32 memdump_addr = 0;
	Uint32 memdump_count = 0;
	int offset = 0;
	const char* err_str = NULL;

	/* For remote debug, only "address" "count" is supported */
	arg = 1;
	if (nArgc >= arg + 2)
	{
		err_str = Eval_Expression(psArgs[arg], &memdump_addr, &offset, false);
		if (err_str)
			return 1;

		++arg;
		err_str = Eval_Expression(psArgs[arg], &memdump_count, &offset, false);
		if (err_str)
			return 1;
		++arg;
	}
	else
	{
		// Not enough args
		return 1;
	}

	send_str(fd, "OK ");
	send_hex(fd, memdump_addr);
	send_str(fd, " ");
	send_hex(fd, memdump_count);
	send_str(fd, " ");

	// Send data in blocks of "buffer_size" memory bytes
	// (We don't need a terminator when sending)
	const uint32_t buffer_size = RDB_MEM_BLOCK_SIZE*2;
	char* buffer = malloc(buffer_size);
	const char* hex = "0123456789ABCDEF";
	uint32_t pos = 0;
	while (pos < memdump_count)
	{
		uint32_t block_size = memdump_count - pos;
		if (block_size > RDB_MEM_BLOCK_SIZE)
			block_size = RDB_MEM_BLOCK_SIZE;

		for (uint32_t i = 0; i < block_size; ++i)
		{
			value = STMemory_ReadByte(memdump_addr);
			buffer[i * 2]     = hex[(value >> 4) & 15];
			buffer[i * 2 + 1] = hex[ value & 15];
			++pos;
			++memdump_addr;
		}
		send(fd, buffer, block_size * 2, 0);
	}

	free(buffer);
	return 0;
}

/**
 * Write the requested area of ST memory.
 *
 * Input: "memset <start addr> <hex-data>\n"
 *
 * Output: "OK"/"NG"
 */

static int RemoteDebug_Memset(int nArgc, char *psArgs[], int fd)
{
	int arg;
	Uint32 memdump_addr = 0;
	Uint32 memdump_end = 0;
	Uint32 memdump_count = 0;
	uint8_t valHi;
	uint8_t valLo;
	int offset = 0;
	const char* err_str = NULL;

	/* For remote debug, only "address" "count" is supported */
	arg = 1;
	if (nArgc >= arg + 3)
	{
		// Address
		err_str = Eval_Expression(psArgs[arg], &memdump_addr, &offset, false);
		if (err_str)
			return 1;

		++arg;
		// Size
		err_str = Eval_Expression(psArgs[arg], &memdump_count, &offset, false);
		if (err_str)
			return 1;
		++arg;
	}
	else
	{
		// Not enough args
		return 1;
	}

	memdump_end = memdump_addr + memdump_count;
	uint32_t pos = 0;
	while (memdump_addr < memdump_end)
	{
		if (!read_hex_char(psArgs[arg][pos], &valHi))
			return 1;
		++pos;
		if (!read_hex_char(psArgs[arg][pos], &valLo))
			return 1;
		++pos;

		//put_byte(memdump_addr, (valHi << 4) | valLo);
		STMemory_WriteByte(memdump_addr, (valHi << 4) | valLo);
		++memdump_addr;
	}
	send_str(fd, "OK ");
	// Report changed range so tools can decide to update
	send_hex(fd, memdump_end - memdump_count);
	send_str(fd, " ");
	send_hex(fd, memdump_count);
	return 0;
}

// -----------------------------------------------------------------------------
/* Set a breakpoint at an address. */
static int RemoteDebug_bp(int nArgc, char *psArgs[], int fd)
{
	int arg = 1;
	if (nArgc >= arg + 1)
	{
		// Pass to standard simple function
		if (BreakCond_Command(psArgs[arg], false))
		{
			send_str(fd, "OK");
			return 0;
		}
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* List all breakpoints */
static int RemoteDebug_bplist(int nArgc, char *psArgs[], int fd)
{
	int i, count;

	count = BreakCond_CpuBreakPointCount();
	send_str(fd, "OK ");
	send_hex(fd, count);
	send_str(fd, " ");

	/* NOTE breakpoint query indices start at 1 */
	for (i = 1; i <= count; ++i)
	{
		bc_breakpoint_query_t query;
		BreakCond_GetCpuBreakpointInfo(i, &query);

		send_str(fd, query.expression);
		/* Note this has the ` character to flag the expression end,
		since the expression can contain spaces */
		send_str(fd, "`");
		send_hex(fd, query.ccount); send_str(fd, " ");
		send_hex(fd, query.hits); send_str(fd, " ");
		send_bool(fd, query.once); send_str(fd, " ");
		send_bool(fd, query.quiet); send_str(fd, " ");
		send_bool(fd, query.trace); send_str(fd, " ");
	}
	return 0;
}

// -----------------------------------------------------------------------------
/* Remove breakpoint number N.
   NOTE breakpoint IDs start at 1!
*/
static int RemoteDebug_bpdel(int nArgc, char *psArgs[], int fd)
{
	int arg = 1;
	Uint32 bp_position;
	if (nArgc >= arg + 1)
	{
		if (Eval_Number(psArgs[arg], &bp_position))
		{
			if (BreakCond_RemoveCpuBreakpoint(bp_position))
			{
				send_str(fd, "OK");
				return 0;
			}
		}
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* "symlist" -- List all CPU symbols */
static int RemoteDebug_symlist(int nArgc, char *psArgs[], int fd)
{
	int i, count;
	count = Symbols_CpuSymbolCount();
	send_str(fd, "OK ");
	send_hex(fd, count);
	send_str(fd, " ");
	
	for (i = 0; i < count; ++i)
	{
		rdb_symbol_t query;
		if (!Symbols_GetCpuSymbol(i, &query))
			break;
		send_str(fd, query.name);
		send_str(fd, "`");
		send_hex(fd, query.address);
		send_str(fd, " ");
		send_char(fd, query.type);
		send_str(fd, " ");
	}
	return 0;
}

// -----------------------------------------------------------------------------
/* "exmask" -- Read or set exception mask */
/* returns "OK <mask val>"" */
static int RemoteDebug_exmask(int nArgc, char *psArgs[], int fd)
{
	int arg = 1;
	Uint32 mask;
	int offset;
	const char* err_str;

	if (nArgc == 2)
	{
		// Assumed to set the mask
		err_str = Eval_Expression(psArgs[arg], &mask, &offset, false);
		if (err_str)
			return 1;
		ExceptionDebugMask = mask;
	}

	// Always respond with the mask value, so that setting comes back
	// to the remote debugger
	send_str(fd, "OK ");
	send_hex(fd, ExceptionDebugMask);
	return 0;
}

// -----------------------------------------------------------------------------
/* DebugUI command structure */
typedef struct
{
	int (*pFunction)(int argc, char *argv[], int fd);
	const char *sName;
	bool split_args;
} rdbcommand_t;

/* Array of all remote debug command descriptors */
static const rdbcommand_t remoteDebugCommandList[] = {
	{ RemoteDebug_Status,   "status"	, true		},
	{ RemoteDebug_Break,    "break"		, true		},
	{ RemoteDebug_Step,     "step"		, true		},
	{ RemoteDebug_Run, 		"run"		, true		},
	{ RemoteDebug_Regs,     "regs"		, true		},
	{ RemoteDebug_Mem,      "mem"		, true		},
	{ RemoteDebug_Memset,   "memset"	, true		},
	{ RemoteDebug_bp, 		"bp"		, false		},
	{ RemoteDebug_bplist,	"bplist"	, true		},
	{ RemoteDebug_bpdel,	"bpdel"		, true		},
	{ RemoteDebug_symlist,	"symlist"	, true		},
	{ RemoteDebug_exmask,	"exmask"	, true		},

	/* Terminator */
	{ NULL, NULL }
};

/**
 * Parse remote debug command and execute it.
 * Command should return 0 if successful.
 * Returns -1 if command not parsed
 */
static int RemoteDebug_Parse(const char *input_orig, int fd)
{
	char *psArgs[64], *input, *input2;
	const char *delim;
	int nArgc = -1;
	int retval;

	input = strdup(input_orig);
	input2 = strdup(input_orig);
	// NO CHECK use the safer form of strtok
	psArgs[0] = strtok(input, " \t");

	/* Search the command ... */
	const rdbcommand_t* pCommand = remoteDebugCommandList;
	while (pCommand->pFunction)
	{
		if (!strcmp(psArgs[0], pCommand->sName))
			break;
		++pCommand;
	}
	if (!pCommand->pFunction)
	{
		free(input);
		return -1;
	}

	delim = " \t";

	/* Separate arguments and put the pointers into psArgs */
	if (pCommand->split_args)
	{
		for (nArgc = 1; nArgc < ARRAY_SIZE(psArgs); nArgc++)
		{
			psArgs[nArgc] = strtok(NULL, delim);
			if (psArgs[nArgc] == NULL)
				break;
		}
	}
	else
	{
		/* Single arg to pass through to some internal calls,
		   for example breakpoint parsing
		*/
		psArgs[1] = input + strlen(psArgs[0]) + 1;
		nArgc = 2;
	}
	
	if (nArgc >= ARRAY_SIZE(psArgs))
	{
		retval = -1;
	}
	else
	{
		/* ... and execute the function */
		retval = pCommand->pFunction(nArgc, psArgs, fd);
	}
	free(input);
	free(input2);
	return retval;
}


// -----------------------------------------------------------------------------
#if HAVE_WINSOCK_SOCKETS
static void SetNonBlocking(SOCKET socket, u_long nonblock)
{
	// Set socket to blocking
	u_long mode = nonblock;  // 0 to enable blocking socket
	ioctlsocket(socket, FIONBIO, &mode);
}
#define GET_SOCKET_ERROR		WSAGetLastError()
#define RDB_CLOSE				closesocket

#endif
#if HAVE_UNIX_DOMAIN_SOCKETS
static void SetNonBlocking(int socket, u_long nonblock)
{
	// Set socket to blocking
	int	on = fcntl(socket, F_GETFL);
	if (nonblock)
		on = (on | O_NONBLOCK);
	else
		on &= ~O_NONBLOCK;
	fcntl(socket, F_SETFL, on);
}

// Set the socket to allow reusing the port. Avoids problems when
// exiting and restarting with hrdb still live.
static void SetReuseAddr(int fd)
{
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
}

#define GET_SOCKET_ERROR		errno
#define RDB_CLOSE				close
#endif

typedef struct RemoteDebugState
{
	int SocketFD;								/* handle for the port/socket */
	int AcceptedFD;								/* handle for the accepted connection from client, or -1 */
	char cmd_buf[REMOTE_DEBUG_CMD_MAX_SIZE+1];	/* accumulated command string */
	int cmd_pos;								/* offset in cmd_buf for new data */
} RemoteDebugState;

static RemoteDebugState g_rdbState;

static void RemoteDebugState_Init(RemoteDebugState* state)
{
	state->SocketFD = -1;
	state->AcceptedFD = -1;
	memset(state->cmd_buf, 0, sizeof(state->cmd_buf));
	state->cmd_pos = 0;
}

/* Process any command data that has been read into the pending
	command buffer, and execute them.
*/
static void RemoteDebug_ProcessBuffer(RemoteDebugState* state)
{
	int cmd_ret;
	while (1)
	{
		// Scan for a complete command
		char* endptr = memchr(state->cmd_buf, 0, state->cmd_pos);
		if (!endptr)
			break;

		int length = endptr - state->cmd_buf;

		const char* pCmd = state->cmd_buf;

		// Process this command
		cmd_ret = RemoteDebug_Parse(pCmd, state->AcceptedFD);

		if (cmd_ret != 0)
		{
			// return an error if something failed
			send_str(state->AcceptedFD, "NG");
		}
		send_term(state->AcceptedFD);

		// Copy extra bytes to the start
		// -1 here is for the terminator
		int extra_length = state->cmd_pos - length - 1;
		memcpy(state->cmd_buf, endptr + 1, extra_length);
		state->cmd_pos = extra_length;
	}
}

/*
	Listen to the connection for remote-debug messages,
	some of which will release us from the break loop
*/
static bool RemoteDebug_BreakLoop(void)
{
	int remaining;
	RemoteDebugState* state;
	fd_set set;
	struct timeval timeout;
#if HAVE_WINSOCK_SOCKETS
	int winerr;
#endif

	// TODO set socket as blocking
	state = &g_rdbState;

	bRemoteBreakIsActive = true;
	// Notify after state change happens
	RemoteDebug_NotifyConfig(state->AcceptedFD);
	RemoteDebug_NotifyState(state->AcceptedFD);

	// Set the socket to blocking on the connection now, so we
	// sleep until data is available.
	SetNonBlocking(state->AcceptedFD, 0);

	timeout.tv_sec = 0;
	timeout.tv_usec = RDB_SELECT_TIMEOUT_USEC;
	while (bRemoteBreakIsActive)
	{
		if (state->AcceptedFD == -1 || 
			state->SocketFD == -1)
		{
			break;
		}

		// Check socket with timeout
		FD_ZERO(&set);
		FD_SET(state->AcceptedFD, &set);

		int rv = select(state->AcceptedFD + 1, &set, NULL, NULL, &timeout);
		if (rv < 0)
		{
			// select error
			break;
		}
		else if (rv == 0)
		{
			// timeout, socket does not have anything to read.
			// Update main event handler so that the UI window can update/redraw.
			Main_EventHandler();
			continue;
		}

		// Read input and accumulate a command (blocking)
		remaining = REMOTE_DEBUG_CMD_MAX_SIZE - state->cmd_pos;
		int bytes = recv(state->AcceptedFD, 
			&state->cmd_buf[state->cmd_pos],
			remaining,
			0);
		if (bytes > 0)
		{
			// New data. Is there a command in there (null-terminated string)
			state->cmd_pos += bytes;
			RemoteDebug_ProcessBuffer(state);
		}
		else if (bytes == 0)
		{
			// This represents an orderly EOF, even in Winsock
			printf("Remote Debug connection closed\n");
			DebugUI_RegisterRemoteDebug(NULL);
			RDB_CLOSE(state->AcceptedFD);
			state->AcceptedFD = -1;

			// Bail out of the loop here so we don't just spin
			break;
		}
		else
		{
			// On Windows -1 simply means a general error and might be OK.
			// So we check for known errors that should cause us to exit.
#if HAVE_WINSOCK_SOCKETS
			winerr = WSAGetLastError();
			if (winerr == WSAECONNRESET)
			{
				printf("Remote Debug connection reset\n");
				state->AcceptedFD = -1;
				break;
			}
			printf("Unknown cmd %d\n", WSAGetLastError());
#endif
		}
	}
	bRemoteBreakIsActive = false;
	// Clear any break request that might have been set
	bRemoteBreakRequest = false;

	RemoteDebug_NotifyConfig(state->AcceptedFD);
	RemoteDebug_NotifyState(state->AcceptedFD);

	SetNonBlocking(state->AcceptedFD, 1);
	return true;
}

/*
	Create a socket for the port and start to listen over TCP
*/
static int RemoteDebugState_InitServer(RemoteDebugState* state)
{
	// Create listening socket on port
	struct sockaddr_in sa;

	state->SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (state->SocketFD == -1) {
		fprintf(stderr, "Failed to open socket\n");
		return 1;
	}
#if HAVE_UNIX_DOMAIN_SOCKETS
	SetReuseAddr(state->SocketFD);
#endif

	// Socket is non-blokcing to start with
	SetNonBlocking(state->SocketFD, 1);

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(REMOTE_DEBUG_PORT);
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(state->SocketFD,(struct sockaddr *)&sa, sizeof sa) == -1) {
		fprintf(stderr, "Failed to bind socket (%d)\n", GET_SOCKET_ERROR);
		RDB_CLOSE(state->SocketFD);
		state->SocketFD =-1;
		return 1;
	}
  
	if (listen(state->SocketFD, 1) == -1) {
		fprintf(stderr, "Failed to listen() on socket\n");
		RDB_CLOSE(state->SocketFD);
		state->SocketFD =-1;
		return 1;
	}

	// Socket is now in a listening state and could accept 
	printf("Remote Debug Listening on port %d\n", REMOTE_DEBUG_PORT);
	return 0;
}

static void RemoteDebugState_Update(RemoteDebugState* state)
{
	if (state->SocketFD == -1)
	{
		// TODO: we should try to re-init periodically
		return;
	}

	int remaining;
	if (state->AcceptedFD != -1)
	{
		// Connection is active
		// Read input and accumulate a command
		remaining = REMOTE_DEBUG_CMD_MAX_SIZE - state->cmd_pos;
        
#if HAVE_UNIX_DOMAIN_SOCKETS
		int bytes = recv(state->AcceptedFD, 
			&state->cmd_buf[state->cmd_pos],
			remaining,
			MSG_DONTWAIT);
#endif
#if HAVE_WINSOCK_SOCKETS
		int bytes = recv(state->AcceptedFD, 
			&state->cmd_buf[state->cmd_pos],
			remaining,
			0);
#endif

		if (bytes > 0)
		{
			// New data. Is there a command in there (null-terminated string)
			state->cmd_pos += bytes;
			RemoteDebug_ProcessBuffer(state);
		}
		else if (bytes == 0)
		{
			// This represents an orderly EOF
			printf("Remote Debug connection closed\n");
			DebugUI_RegisterRemoteDebug(NULL);
			RDB_CLOSE(state->AcceptedFD);
			state->AcceptedFD = -1;
			return;
		}
	}
	else
	{
		// Active accepted socket
		state->AcceptedFD = accept(state->SocketFD, NULL, NULL);
		if (state->AcceptedFD != -1)
		{
			printf("Remote Debug connection accepted\n");
			DebugUI_RegisterRemoteDebug(RemoteDebug_BreakLoop);
			SetNonBlocking(state->AcceptedFD, 1);

			// Send connected handshake, so client can
			// drop any subsequent commands
			send_str(state->AcceptedFD, "!connected");
			send_term(state->AcceptedFD);

			// Flag the running status straight away
			RemoteDebug_NotifyConfig(state->AcceptedFD);
			RemoteDebug_NotifyState(state->AcceptedFD);
		}
	}
}

void RemoteDebug_Init(void)
{
	printf("Starting remote debug\n");
	
#if HAVE_WINSOCK_SOCKETS

    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(1, 0);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        /* Tell the user that we could not find a usable */
        /* Winsock DLL.                                  */
        printf("WSAStartup failed with error: %d\n", err);
		
		// NO CHECK clean up state
        return;
    }
#endif

	RemoteDebugState_Init(&g_rdbState);
	RemoteDebugState_InitServer(&g_rdbState);
}

void RemoteDebug_UnInit()
{
	printf("Stopping remote debug\n");
	if (g_rdbState.AcceptedFD != -1)
	{
		RDB_CLOSE(g_rdbState.AcceptedFD);
		g_rdbState.AcceptedFD = -1;
	}

	if (g_rdbState.SocketFD != -1)
	{
		RDB_CLOSE(g_rdbState.SocketFD);
		g_rdbState.SocketFD = -1;
	}
}

bool RemoteDebug_Update(void)
{
	// This function is called from the main event handler, which
	// is also called while break is active. So protect against
	// re-entrancy.
	if (!bRemoteBreakIsActive)
	{
		RemoteDebugState_Update(&g_rdbState);
	}
	return bRemoteBreakIsActive;
}

/**
 * Debugger invocation if requested by remote debugger.
 * 
 * NOTE: we could just run our own loop here, rather than going through DebugUI?
 */
void RemoteDebug_CheckRemoteBreak(void)
{
	if (bRemoteBreakRequest)
	{
		bRemoteBreakRequest = false;
		// Stop and wait for inputs from the control socket
		DebugUI(REASON_USER);
	}
}
