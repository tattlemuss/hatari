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
#include "debug_priv.h"	/* For debugOutput control */
#include "debugcpu.h"	/* For stepping */
#include "evaluate.h"
#include "stMemory.h"
#include "breakcond.h"
#include "symbols.h"
#include "log.h"
#include "vars.h"
#include "memory.h"
#include "configuration.h"

// For status bar updates
#include "screen.h"
#include "statusbar.h"

// TCP port for remote debugger access
#define RDB_PORT                   (56001)

// Max character count in a command sent to Hatari
#define RDB_CMD_MAX_SIZE           (300)

// How many bytes we collect to send chunks for the "mem" command
#define RDB_MEM_BLOCK_SIZE         (2048)

// How many bytes in the internal network send buffer
#define RDB_SEND_BUFFER_SIZE       (512)

// Network timeout when in break loop, to allow event handler update.
// Currently 0.5sec
#define RDB_SELECT_TIMEOUT_USEC   (500000)

/* Remote debugging break command was sent from debugger */
static bool bRemoteBreakRequest = false;

/* Processing is stopped and the remote debug loop is active */
static bool bRemoteBreakIsActive = false;

// -----------------------------------------------------------------------------
// Structure managing connection state
typedef struct RemoteDebugState
{
	int SocketFD;						/* handle for the port/socket. -1 if not available */
	int AcceptedFD;						/* handle for the accepted connection from client, or
											-1 if not connected */

	/* Input (receive/command) buffer data */
	char cmd_buf[RDB_CMD_MAX_SIZE+1];	/* accumulated command string */
	int cmd_pos;						/* offset in cmd_buf for new data */

	FILE* original_stdout;				/* original file pointers for redirecting output */
	FILE* original_stderr;
	FILE* original_debugOutput;
	FILE* debugOutput;					/* our file handle to output */

	/* Output (send) buffer data */
	char sendBuffer[RDB_SEND_BUFFER_SIZE];	/* buffer for replies */
	int sendBufferPos;					/* next byte to write into buffer */
} RemoteDebugState;

// -----------------------------------------------------------------------------
// Force send of data in sendBuffer
static void flush_data(RemoteDebugState* state)
{
	// Flush existing data
	send(state->AcceptedFD, state->sendBuffer, state->sendBufferPos, 0);
	state->sendBufferPos = 0;
}

// -----------------------------------------------------------------------------
// Add data to sendBuffer, flush if necessary
static void add_data(RemoteDebugState* state, const char* data, size_t size)
{
	// Flush data if it won't fit
	if (state->sendBufferPos + size > RDB_SEND_BUFFER_SIZE)
		flush_data(state);

	memcpy(state->sendBuffer + state->sendBufferPos, data, size);
	state->sendBufferPos += size;
}

// -----------------------------------------------------------------------------
// Transmission functions (wrapped for platform portability)
// -----------------------------------------------------------------------------
static void send_str(RemoteDebugState* state, const char* pStr)
{
	add_data(state, pStr, strlen(pStr));
}

// -----------------------------------------------------------------------------
static void send_hex(RemoteDebugState* state, uint32_t val)
{
	char str[9];
	int size = sprintf(str, "%X", val);
	add_data(state, str, size);
}

// -----------------------------------------------------------------------------
static void send_char(RemoteDebugState* state, char val)
{
	add_data(state, &val, 1);
}

// -----------------------------------------------------------------------------
static void send_bool(RemoteDebugState* state, bool val)
{
	send_char(state, val ? '1' : '0');
}

// -----------------------------------------------------------------------------
static void send_key_value(RemoteDebugState* state, const char* pStr, uint32_t val)
{
	send_str(state, " ");
	send_str(state, pStr);
	send_str(state, ":");
	send_hex(state, val);
}

// -----------------------------------------------------------------------------
static void send_term(RemoteDebugState* state)
{
	send_char(state, 0);
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
static int RemoteDebug_NotifyState(RemoteDebugState* state)
{
	char tmp[100];
	sprintf(tmp, "!status %x %x", bRemoteBreakIsActive ? 0 : 1, M68000_GetPC());
	send_str(state, tmp);
	send_term(state);
	return 0;
}

// -----------------------------------------------------------------------------
static int RemoteDebug_NotifyConfig(RemoteDebugState* state)
{
	const CNF_SYSTEM* system = &ConfigureParams.System;
	char tmp[100];
	sprintf(tmp, "!config %x %x", 
		system->nMachineType, system->nCpuLevel);
	
	send_str(state, tmp);
	send_term(state);
	return 0;
}

// -----------------------------------------------------------------------------
/* Restore any debugOutput settings to the original saved state. Close
   any file we opened. */
static void RemoteDebug_CloseDebugOutput(RemoteDebugState* state)
{
	/* Restore old stdio, if set */
#ifndef __WINDOWS__
	if (state->original_stderr != NULL)
		stderr = state->original_stderr;
	if (state->original_stdout != NULL)
		stdout = state->original_stdout;
	if (state->original_debugOutput != NULL)
		stdout = state->original_debugOutput;
	if (state->debugOutput)
		fclose(state->debugOutput);
#endif
	state->original_stderr = NULL;
	state->original_stdout = NULL;
	state->original_debugOutput = NULL;
	state->debugOutput = NULL;
}

// -----------------------------------------------------------------------------
//    DEBUGGER COMMANDS
// -----------------------------------------------------------------------------
/* Return short status info in a useful format, mainly whether it's running */
static int RemoteDebug_Status(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	char tmp[100];
	sprintf(tmp, "OK %x %x", bRemoteBreakIsActive ? 0 : 1, M68000_GetPC());
	send_str(state, tmp);
	return 0;
}

// -----------------------------------------------------------------------------
/* Put in a break request which is serviced elsewhere in the main loop */
static int RemoteDebug_Break(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	// Only set a break request if we are running
	if (!bRemoteBreakIsActive)
	{
		bRemoteBreakRequest = true;
		send_str(state, "OK");
	}
	else
	{
		return 1;
	}
	
	return 0;
}

// -----------------------------------------------------------------------------
/* Step next instruction. This is currently a passthrough to the normal debugui code. */
static int RemoteDebug_Step(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	DebugCpu_SetSteps(1);
	send_str(state, "OK");

	// Restart
	bRemoteBreakIsActive = false;
	return 0;
}

// -----------------------------------------------------------------------------
static int RemoteDebug_Run(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	send_str(state, "OK");
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
static int RemoteDebug_Regs(int nArgc, char *psArgs[], RemoteDebugState* state)
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

	send_str(state, "OK ");

	// Normal regs
	for (regIdx = 0; regIdx < ARRAY_SIZE(regIds); ++regIdx)
		send_key_value(state, regNames[regIdx], Regs[regIds[regIdx]]);
		
	// Special regs
	send_key_value(state, "PC", M68000_GetPC());
	send_key_value(state, "USP", regs.usp);
	send_key_value(state, "ISP", regs.isp);
	send_key_value(state, "SR", M68000_GetSR());
	send_key_value(state, "EX", regs.exception);

	// Variables
	while (Vars_QueryVariable(varIndex, &var))
	{
		Uint32 value;
		value = Vars_GetValue(var);
		send_key_value(state, var->name, value);
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

static int RemoteDebug_Mem(int nArgc, char *psArgs[], RemoteDebugState* state)
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

	send_str(state, "OK ");
	send_hex(state, memdump_addr);
	send_str(state, " ");
	send_hex(state, memdump_count);
	send_str(state, " ");

	// Need to flush here before we switch to our existing buffer system
	flush_data(state);

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
		send(state->AcceptedFD, buffer, block_size * 2, 0);
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

static int RemoteDebug_Memset(int nArgc, char *psArgs[], RemoteDebugState* state)
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
	send_str(state, "OK ");
	// Report changed range so tools can decide to update
	send_hex(state, memdump_end - memdump_count);
	send_str(state, " ");
	send_hex(state, memdump_count);
	return 0;
}

// -----------------------------------------------------------------------------
/* Set a breakpoint at an address. */
static int RemoteDebug_bp(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg = 1;
	if (nArgc >= arg + 1)
	{
		// Pass to standard simple function
		if (BreakCond_Command(psArgs[arg], false))
		{
			send_str(state, "OK");
			return 0;
		}
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* List all breakpoints */
static int RemoteDebug_bplist(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int i, count;

	count = BreakCond_CpuBreakPointCount();
	send_str(state, "OK ");
	send_hex(state, count);
	send_str(state, " ");

	/* NOTE breakpoint query indices start at 1 */
	for (i = 1; i <= count; ++i)
	{
		bc_breakpoint_query_t query;
		BreakCond_GetCpuBreakpointInfo(i, &query);

		send_str(state, query.expression);
		/* Note this has the ` character to flag the expression end,
		since the expression can contain spaces */
		send_str(state, "`");
		send_hex(state, query.ccount); send_str(state, " ");
		send_hex(state, query.hits); send_str(state, " ");
		send_bool(state, query.once); send_str(state, " ");
		send_bool(state, query.quiet); send_str(state, " ");
		send_bool(state, query.trace); send_str(state, " ");
	}
	return 0;
}

// -----------------------------------------------------------------------------
/* Remove breakpoint number N.
   NOTE breakpoint IDs start at 1!
*/
static int RemoteDebug_bpdel(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg = 1;
	Uint32 bp_position;
	if (nArgc >= arg + 1)
	{
		if (Eval_Number(psArgs[arg], &bp_position))
		{
			if (BreakCond_RemoveCpuBreakpoint(bp_position))
			{
				send_str(state, "OK");
				return 0;
			}
		}
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* "symlist" -- List all CPU symbols */
static int RemoteDebug_symlist(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int i, count;
	count = Symbols_CpuSymbolCount();
	send_str(state, "OK ");
	send_hex(state, count);
	send_str(state, " ");
	
	for (i = 0; i < count; ++i)
	{
		rdb_symbol_t query;
		if (!Symbols_GetCpuSymbol(i, &query))
			break;
		send_str(state, query.name);
		send_str(state, "`");
		send_hex(state, query.address);
		send_str(state, " ");
		send_char(state, query.type);
		send_str(state, " ");
	}
	return 0;
}

// -----------------------------------------------------------------------------
/* "exmask" -- Read or set exception mask */
/* returns "OK <mask val>"" */
static int RemoteDebug_exmask(int nArgc, char *psArgs[], RemoteDebugState* state)
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
	send_str(state, "OK ");
	send_hex(state, ExceptionDebugMask);
	return 0;
}

// -----------------------------------------------------------------------------
/* "console <text>" pass command to debugui console input */
/* returns "OK" */
static int RemoteDebug_console(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	if (nArgc == 2)
	{
		int cmdRet = DebugUI_ParseConsoleCommand(psArgs[1]);

		/* handle a command that restarts execution */
		if (cmdRet == DEBUGGER_END)
			bRemoteBreakIsActive = false;

		fflush(debugOutput);
		fflush(stderr);
		// Insert an out-of-band notification, in case of restart
		RemoteDebug_NotifyState(state);
	}
	send_str(state, "OK");
	return 0;
}

// -----------------------------------------------------------------------------
/* "setstd <filename> redirects stdout/stderr to given file */
/* returns "OK"/"NG" */
static int RemoteDebug_setstd(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	if (nArgc == 2)
	{
		// Create the output file
		const char* filename = psArgs[1];
		FILE* outpipe = fopen(filename, "w");
		if (outpipe)
		{
			// Switch back to "normal settings"
			RemoteDebug_CloseDebugOutput(state);

			// Record the original states
			state->original_stderr = stderr;
			state->original_stdout = stdout;
			state->original_debugOutput = debugOutput;

			// Switch over redirect
#ifdef __WINDOWS__
			freopen(filename, "w", stdout);
			freopen(filename, "w", stderr);
#else
			stderr = outpipe;
			stdout = outpipe;
#endif
			debugOutput = outpipe;
			state->debugOutput = outpipe;
			send_str(state, "OK");
			return 0;
		}
	}
	return 1;
}
// -----------------------------------------------------------------------------
/* DebugUI command structure */
typedef struct
{
	int (*pFunction)(int argc, char *argv[], RemoteDebugState* state);
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
	{ RemoteDebug_console,	"console"	, false		},
	{ RemoteDebug_setstd,	"setstd"	, true		},

	/* Terminator */
	{ NULL, NULL }
};

/**
 * Parse remote debug command and execute it.
 * Command should return 0 if successful.
 * Returns -1 if command not parsed
 */
static int RemoteDebug_Parse(const char *input_orig, RemoteDebugState* state)
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
		retval = pCommand->pFunction(nArgc, psArgs, state);
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

static RemoteDebugState g_rdbState;

static void RemoteDebugState_Init(RemoteDebugState* state)
{
	state->SocketFD = -1;
	state->AcceptedFD = -1;
	memset(state->cmd_buf, 0, sizeof(state->cmd_buf));
	state->cmd_pos = 0;
	state->original_stdout = NULL;
	state->original_stderr = NULL;
	state->original_debugOutput = NULL;
	state->debugOutput = NULL;
	state->sendBufferPos = 0;
}

static int RemoteDebugState_TryAccept(RemoteDebugState* state, bool blocking)
{
	fd_set set;
	struct timeval timeout;

	if (blocking)
	{
		// Connection active
		// Check socket with timeout
		FD_ZERO(&set);
		FD_SET(state->SocketFD, &set);

		// On Linux, need to reset the timeout on each loop
		// see "select(2)"
		timeout.tv_sec = 0;
		timeout.tv_usec = RDB_SELECT_TIMEOUT_USEC;
		int rv = select(state->SocketFD + 1, &set, NULL, NULL, &timeout);
		if (rv <= 0)
			return state->AcceptedFD;
	}

	state->AcceptedFD = accept(state->SocketFD, NULL, NULL);
	if (state->AcceptedFD != -1)
	{
		printf("Remote Debug connection accepted\n");
		// reset send buffer
		state->sendBufferPos = 0;
		// Send connected handshake, so client can
		// drop any subsequent commands
		send_str(state, "!connected");
		send_term(state);
		flush_data(state);

		// New connection, so do an initial report.
		RemoteDebug_NotifyConfig(state);
		RemoteDebug_NotifyState(state);
		flush_data(state);
	}
	return state->AcceptedFD;
}

/* Process any command data that has been read into the pending
	command buffer, and execute them.
*/
static void RemoteDebug_ProcessBuffer(RemoteDebugState* state)
{
	int cmd_ret;
	int num_commands = 0;
	while (1)
	{
		// Scan for a complete command
		char* endptr = memchr(state->cmd_buf, 0, state->cmd_pos);
		if (!endptr)
			break;

		int length = endptr - state->cmd_buf;

		const char* pCmd = state->cmd_buf;

		// Process this command
		cmd_ret = RemoteDebug_Parse(pCmd, state);

		if (cmd_ret != 0)
		{
			// return an error if something failed
			send_str(state, "NG");
		}
		send_term(state);

		// Copy extra bytes to the start
		// -1 here is for the terminator
		int extra_length = state->cmd_pos - length - 1;
		memcpy(state->cmd_buf, endptr + 1, extra_length);
		state->cmd_pos = extra_length;
		++num_commands;
	}

	if (num_commands)
		flush_data(state);
}

/*	Handle activity from the accepted connection.
	Disconnect if socket lost or other errors.
*/
static void RemoteDebugState_UpdateAccepted(RemoteDebugState* state)
{
	int remaining;
	fd_set set;
	struct timeval timeout;
#if HAVE_WINSOCK_SOCKETS
	int winerr;
#endif

	// Connection active
	// Check socket with timeout
	FD_ZERO(&set);
	FD_SET(state->AcceptedFD, &set);

	// On Linux, need to reset the timeout on each loop
	// see "select(2)"
	timeout.tv_sec = 0;
	timeout.tv_usec = RDB_SELECT_TIMEOUT_USEC;

	int rv = select(state->AcceptedFD + 1, &set, NULL, NULL, &timeout);
	if (rv < 0)
	{
		// select error, Lost connection?
		return;
	}
	else if (rv == 0)
	{
		// timeout, socket does not have anything to read.
		// Run event handler while we know nothing changes
		Main_EventHandler(true);
		return;
	}

	// Read input and accumulate a command (blocking)
	remaining = RDB_CMD_MAX_SIZE - state->cmd_pos;
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
		RDB_CLOSE(state->AcceptedFD);
		state->AcceptedFD = -1;

		// Bail out of the loop here so we don't just spin
		return;
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
			RDB_CLOSE(state->AcceptedFD);
			state->AcceptedFD = -1;
		}
		printf("Unknown cmd %d\n", WSAGetLastError());
#endif
	}
}

/* Update with a suitable message, when we are in the break loop */
static void SetStatusbarMessage(const RemoteDebugState* state)
{
	if (state->AcceptedFD != -1)
		Statusbar_AddMessage("hrdb connected -- debugging", 100);
	else
		Statusbar_AddMessage("break -- waiting for hrdb", 100);
	Statusbar_Update(sdlscrn, true);
}

/*
	Handle the loop once Hatari enters break mode, and update
	network connections and commands/responses. Also calls
	main system event handler (at intervals) to keep the UI
	responsive.

	Exits when
	- commands cause a restart
	- main socket lost
	- quit request from UI
*/
static bool RemoteDebug_BreakLoop(void)
{
	RemoteDebugState* state;
	state = &g_rdbState;

	// This is set to true to prevent re-entrancy in RemoteDebug_Update()
	bRemoteBreakIsActive = true;

	if (state->AcceptedFD != -1)
	{
		// Notify after state change happens
		RemoteDebug_NotifyConfig(state);
		RemoteDebug_NotifyState(state);
		flush_data(state);
	}

	SetStatusbarMessage(state);

	// Set the socket to blocking on the connection now, so we
	// sleep until data is available.
	SetNonBlocking(state->AcceptedFD, 0);

	while (bRemoteBreakIsActive)
	{
		// Handle main exit states
		if (state->SocketFD == -1)
			break;

		if (bQuitProgram)
			break;

		// If we don't have a connection yet, we need to get one
		if (state->AcceptedFD == -1)
		{
			// Try to reconnect (with select())
			RemoteDebugState_TryAccept(state, true);
			if (state->AcceptedFD != -1)
			{
				// Set the socket to blocking on the connection now, so we
				// sleep until data is available.
				SetNonBlocking(state->AcceptedFD, 0);
				SetStatusbarMessage(state);
			}
			else
			{
				// No connection so update events
				Main_EventHandler(true);
			}
		}
		else
		{
			// Already connected, check for messages or disconnection
			RemoteDebugState_UpdateAccepted(state);
			if (state->AcceptedFD == -1)
			{
				// disconnected
				SetStatusbarMessage(state);
			}
		}
	}
	bRemoteBreakIsActive = false;
	// Clear any break request that might have been set
	bRemoteBreakRequest = false;

	// Switch back to non-blocking for the update loop
	if (state->AcceptedFD != -1)
	{
		RemoteDebug_NotifyConfig(state);
		RemoteDebug_NotifyState(state);
		flush_data(state);

		SetNonBlocking(state->AcceptedFD, 1);
	}

	// TODO: this return code no longer used
	return true;
}

/*
	Create a socket for the port and start to listen over TCP
*/
static int RemoteDebugState_InitServer(RemoteDebugState* state)
{
	state->AcceptedFD = -1;

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
	sa.sin_port = htons(RDB_PORT);
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
	printf("Remote Debug Listening on port %d\n", RDB_PORT);
	return 0;
}

// This is the per-frame update, to check for new connections
// while Hatari is running at full speed
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
		remaining = RDB_CMD_MAX_SIZE - state->cmd_pos;
		
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
			RDB_CLOSE(state->AcceptedFD);
			state->AcceptedFD = -1;
			return;
		}
	}
	else
	{
		if (RemoteDebugState_TryAccept(state, false) != -1)
		{
			// Set this connection to non-blocking since we are
			// in "running" state and it will poll every VBL
			SetNonBlocking(state->AcceptedFD, 1);
			return;
		}
	}
}

void RemoteDebug_Init(void)
{
	printf("Starting remote debug\n");
	RemoteDebugState_Init(&g_rdbState);
	
#if HAVE_WINSOCK_SOCKETS
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;

	wVersionRequested = MAKEWORD(1, 0);
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0)
	{
		printf("WSAStartup failed with error: %d\n", err);
		// No socket can be made, and initial state will
		// still reflect this (SocketFD == -1)
		return;
	}
#endif

	if (RemoteDebugState_InitServer(&g_rdbState) == 0)
	{
		// Socket created, so use our break loop
		DebugUI_RegisterRemoteDebug(RemoteDebug_BreakLoop);
	}
}

void RemoteDebug_UnInit()
{
	printf("Stopping remote debug\n");
	DebugUI_RegisterRemoteDebug(NULL);

	RemoteDebug_CloseDebugOutput(&g_rdbState);
	if (g_rdbState.AcceptedFD != -1)
	{
		RDB_CLOSE(g_rdbState.AcceptedFD);
	}

	if (g_rdbState.SocketFD != -1)
	{
		RDB_CLOSE(g_rdbState.SocketFD);
	}

	g_rdbState.AcceptedFD = -1;
	g_rdbState.SocketFD = -1;
	g_rdbState.cmd_pos = 0;
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
