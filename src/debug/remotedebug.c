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

#include "main.h"		/* For ARRAY_SIZE, event handler */
#include "m68000.h"		/* Must be after main.h for "unlikely" */
#include "debugui.h"	/* For DebugUI_RegisterRemoteDebug */
#include "debug_priv.h"	/* For debugOutput control */
#include "debugcpu.h"	/* For stepping */
#include "debugdsp.h"	/* For stepping */
#include "stMemory.h"
#include "breakcond.h"
#include "symbols.h"
#include "log.h"
#include "vars.h"
#include "memory.h"
#include "configuration.h"
#include "psg.h"
#include "dmaSnd.h"
#include "blitter.h"
#include "dsp.h"
#include "dsp_cpu.h"
#include "profile.h"
// For status bar updates
#include "screen.h"
#include "statusbar.h"
#include "video.h"	/* FIXME: video.h is dependent on HBL_PALETTE_LINES from screen.h */
#include "reset.h"

// TCP port for remote debugger access
#define RDB_PORT                   (56001)

// Max character count in a command sent to Hatari
#define RDB_INPUT_TMP_SIZE         (300)

// Starting size of the growing buffer containing commands to process
#define RDB_CMD_BUFFER_START_SIZE  (512)

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

/* ID of a protocol for the transfers, so we can detect hatari<->mismatch in future */
/* 0x1003 -- add reset commands */
/* 0x1004    add ffwd command, and ffwd status in NotifyStatus() */
/* 0x1005    add memfind command, add stramsize to $config notification */
/* 0x1006    use hex only for address/size in mem[*], bpdel, exmask commands */
/* 0x1007    add savebin */
/* 0x1008    add dmem, DSP support in NotifyConfig */
#define REMOTEDEBUG_PROTOCOL_ID	(0x1008)

/* Char ID to denote terminator of a token. This is under the ASCII "normal"
	character value range so that 32-255 can be used */
#define SEPARATOR_VAL	0x1

/* Forward declaration of callback */
void RemoteDebug_SymbolsChanged(void);

// -----------------------------------------------------------------------------
static bool IsDspActive(void)
{
	return ConfigureParams.System.nDSPType == DSP_TYPE_EMU;
}

// -----------------------------------------------------------------------------
// Structure managing a resizeable buffer of uint8_t
// This can be used to accumulate input commands, or sections of it
// like search strings.
typedef struct RemoteDebugBuffer
{
	char*		data;		// base of allocated data buffer
	size_t		size;		// allocated size of data
	size_t		write_pos;	// current position in buffer
} RemoteDebugBuffer;

// -----------------------------------------------------------------------------
static void RemoteDebugBuffer_Init(RemoteDebugBuffer* buf, size_t size)
{
	buf->size = size;
	buf->data = (char*)malloc(size);
	buf->write_pos = 0;
}

// -----------------------------------------------------------------------------
static void RemoteDebugBuffer_UnInit(RemoteDebugBuffer* buf)
{
	free(buf->data);
	buf->data = NULL;
	buf->size = 0;
	buf->write_pos = 0;
}

// -----------------------------------------------------------------------------
// Copy <size> chars of <data> into the end of the buffer. Resize the buffer if space is not enough.
static void RemoteDebugBuffer_Add(RemoteDebugBuffer* buf, const char* data, size_t size)
{
	assert(buf->write_pos <= buf->size);

	// Full buffer?
	if (buf->write_pos + size > buf->size)
	{
		// Allocate a new buffer bigger than request
		size_t new_size = buf->write_pos + size + 512;
		char* new_data = (char*)malloc(new_size);

		// Copy across (valid) contents and release the original
		memcpy(new_data, buf->data, buf->write_pos);
		free(buf->data);

		// Swap pointers
		buf->size = new_size;
		buf->data = new_data;
	}

	// Copy data in at the write pointer
	memcpy(buf->data + buf->write_pos, data, size);
	buf->write_pos += size;
}

// -----------------------------------------------------------------------------
// Remove <count> bytes from the start of the buffer, and copy the remaining data along
static void RemoteDebugBuffer_RemoveStart(RemoteDebugBuffer* buf, size_t count)
{
	// Calc number of bytes between "count" and the write head
	size_t copy_size = buf->write_pos - count;

	// Copy from position "count" to 0
	// NOTE: uses memmove() since the areas overlap.
	memmove(buf->data, buf->data + count, copy_size);

	// Move the write_pos to the left by "count" chars
	// The allocation size is left unchanged.
	buf->write_pos -= count;
}

// -----------------------------------------------------------------------------
// Structure managing connection state
typedef struct RemoteDebugState
{
	int SocketFD;						/* handle for the port/socket. -1 if not available */
	int AcceptedFD;						/* handle for the accepted connection from client, or
											-1 if not connected */

	/* Input (receive/command) buffer data */
	RemoteDebugBuffer input_buf;
	/* Temp data for network recv */
	char cmd_buf[RDB_INPUT_TMP_SIZE];

	/* Redirection info when running Console window commands */
	FILE* original_debugOutput;				/* This is easy to save and repoint */
#ifdef __WINDOWS__
	char consoleOutputFilename[FILENAME_MAX+1];	/* output filename for redirected output */
#else
	FILE* original_stdout;					/* original file pointers for redirecting output */
	FILE* original_stderr;
	FILE* consoleOutputFile;				/* our file handle to output */
#endif

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
// Send the normal parameter separator (0x1)
static void send_sep(RemoteDebugState* state)
{
	send_char(state, SEPARATOR_VAL);
}

// -----------------------------------------------------------------------------
static void send_key_value(RemoteDebugState* state, const char* pStr, uint32_t val)
{
	send_sep(state);
	send_str(state, pStr);
	send_sep(state);
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
// Read a value of [1..8] hex chars, zero-terminated.
// Returns false if no correct match is made.
static bool read_hex32_value(const char* c, uint32_t* result)
{
	uint32_t acc = 0;
	uint8_t val = 0;
	int count;
	*result = 0;

	// Check for string with no hex chars in
	if (!read_hex_char(*c, &val))
		return false;

	for (count = 0; count < 8; ++count)
	{
		if (!read_hex_char(*c, &val))
			break;
		acc <<= 4;
		acc |= val;
		++c;
	}
	*result = acc;

	// We expect the string to end with a zero-terminator, or it's an error.
	return *c == 0;
}

// -----------------------------------------------------------------------------
// Send the out-of-band status to flag start/stop
// Format: "!status <break active> <pc> <dsp-pc> <ffwd>"
static int RemoteDebug_NotifyState(RemoteDebugState* state)
{
	send_str(state, "!status");
	send_sep(state);
	send_hex(state, bRemoteBreakIsActive ? 0 : 1);
	send_sep(state);
	// PC values are to help fetch disassembly of next instruction quickly.
	send_hex(state, M68000_GetPC());
	send_sep(state);
	send_hex(state, DSP_GetPC());
	send_sep(state);
	send_hex(state, ConfigureParams.System.bFastForward ? 1 : 0);
	send_term(state);
	return 0;
}

// -----------------------------------------------------------------------------
// Format: "!config <machinetype> <cpulevel> <stramend>
static int RemoteDebug_NotifyConfig(RemoteDebugState* state)
{
	const CNF_SYSTEM* system = &ConfigureParams.System;
	send_str(state, "!config");
	send_sep(state);
	send_hex(state, system->nMachineType);
	send_sep(state);
	send_hex(state, system->nCpuLevel);
	send_sep(state);
	send_hex(state, STRamEnd);
	send_sep(state);
	send_hex(state, IsDspActive());

	send_term(state);
	return 0;
}

static void RemoteDebug_NotifyProfile(RemoteDebugState* state)
{
	int index;
	ProfileLine result;
	uint32_t lastaddr;

	send_str(state, "!profile");
	send_sep(state);
	send_hex(state, Profile_CpuIsEnabled() ? 1 : 0);
	send_sep(state);
	
	index = 0;
	lastaddr = 0;
	while (Profile_CpuQuery(index, &result))
	{
		if (result.count)
		{
			// NOTE: address is encoded as delta from previous
			// entry, starting from 0. This provides a very simple
			// size reduction.
			send_hex(state, result.addr - lastaddr);
			send_sep(state);
			send_hex(state, result.count);
			send_sep(state);
			send_hex(state, result.cycles);
			send_sep(state);
			lastaddr = result.addr;
		}
		++index;
	}
	send_term(state);
}

// -----------------------------------------------------------------------------
// Send the out-of-band status to flag that symbols have changed
// because a program was loaded or unloaded.
static void RemoteDebug_NotifySymbols(RemoteDebugState* state)
{
	const char* path;
	// This can be called with no active connection, so skip if necessary.
	if (state->AcceptedFD == -1)
		return;

	path = Symbols_CpuGetCurrentPath();
	if (!path)
		path = "";

	send_str(state, "!symbols");
	send_sep(state);
	send_str(state, path);
	send_term(state);
}

// -----------------------------------------------------------------------------
/* Repoint stderr and debugOutput to the file specified in the state. */
static void RemoteDebug_OpenDebugOutput(RemoteDebugState* state)
{
	// NOTE: debugOutput is for "m" and "d", "r"
	// stderr is for breakpoints, "info"
	state->original_debugOutput = debugOutput;

#ifdef __WINDOWS__
	// Handle output not being set
	if (state->consoleOutputFilename[0] == 0)
		return;

	// Repoint stdout etc to our temporary file again
	// Use the "append" mode, otherwise the file is reset,
	// and this confuses the hrdb tool whose read pointer
	// might be at the end of the old file...
	freopen(state->consoleOutputFilename, "a", stdout);
	freopen(state->consoleOutputFilename, "a", stderr);
	debugOutput = stderr;
#else
	// Save old values for restore
	state->original_stderr = stderr;
	state->original_stdout = stdout;

	// Handle output not being set
	if (!state->consoleOutputFile)
		return;

	// Repoint
	debugOutput = state->consoleOutputFile;
	stderr = state->consoleOutputFile;
	stdout = state->consoleOutputFile;
#endif
}

// -----------------------------------------------------------------------------
/* Restore any debugOutput settings to the original saved state. Close
   any file we opened. */
static void RemoteDebug_CloseDebugOutput(RemoteDebugState* state)
{
	/* Restore old stdio, if set */
#ifdef __WINDOWS__
	freopen("CON", "w", stdout);
	freopen("CON", "w", stderr);
#else
	stderr = state->original_stderr;
	stdout = state->original_stdout;
	state->original_stderr = NULL;
	state->original_stdout = NULL;
#endif
	debugOutput = state->original_debugOutput;
	state->original_debugOutput = NULL;
}

/* Call per-system methods to make sure that any state inspected
   is in sync with what the user sees e.g. hardware registers look
   correct when read, CPU registers are up-to-date.
*/
static void RemoteDebug_HardwareSync(void)
{
	/*
	Workaround to match the behaviour in DebugCpu_Register().
	The call to m68k_dumpstate_file actually *updates* these registers,
	so they are stale until you view the registers in the command line
	debugger (and possibly stale when evaluating breakpoints.)
	*/
	if (regs.s == 0)
		regs.usp = regs.regs[REG_A7];
	if (regs.s && regs.m)
		regs.msp = regs.regs[REG_A7];
	if (regs.s && regs.m == 0)
		regs.isp = regs.regs[REG_A7];

	DmaSnd_RemoteDebugSync();
	Video_RemoteDebugSync();
	Blitter_RemoteDebugSync();
}

// -----------------------------------------------------------------------------
//    DEBUGGER COMMANDS
// -----------------------------------------------------------------------------
/* Return short status info in a useful format, mainly whether it's running */
// Format: "OK <break active> <pc>"
static int RemoteDebug_status(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	send_str(state, "OK");
	send_sep(state);
	send_hex(state, bRemoteBreakIsActive ? 0 : 1);
	send_sep(state);
	send_hex(state, M68000_GetPC());
	send_term(state);
	return 0;
}

// -----------------------------------------------------------------------------
/* Put in a break request which is serviced elsewhere in the main loop */
static int RemoteDebug_break(int nArgc, char *psArgs[], RemoteDebugState* state)
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
static int RemoteDebug_step(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	DebugCpu_SetSteps(1);
	send_str(state, "OK");

	// Restart
	bRemoteBreakIsActive = false;
	return 0;
}

// -----------------------------------------------------------------------------
/* Step next instruction. This is currently a passthrough to the normal debugui code. */
static int RemoteDebug_dstep(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	DebugDsp_SetSteps(1);
	send_str(state, "OK");

	// Restart
	bRemoteBreakIsActive = false;
	return 0;
}

// -----------------------------------------------------------------------------
static int RemoteDebug_run(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	send_str(state, "OK");
	bRemoteBreakIsActive = false;
	return 0;
}

// -----------------------------------------------------------------------------
// Register names and IDs for DSP
typedef struct RemoteDebugDspReg
{
	uint8_t		regId;
	const char*	regName;
}  RemoteDebugDspReg;

#define STRINGIFY(x) #x
#define DSP_REG(id)  { DSP_REG_##id	, "D_" STRINGIFY(id) }

static const RemoteDebugDspReg g_remoteDebugDspRegs[] =
{
	DSP_REG(X0), DSP_REG(X1),
	DSP_REG(Y0), DSP_REG(Y1),
	DSP_REG(A0), DSP_REG(B0),
	DSP_REG(A2), DSP_REG(B2),
	DSP_REG(A1), DSP_REG(B1),

	DSP_REG(R0), DSP_REG(R1), DSP_REG(R2), DSP_REG(R3),
	DSP_REG(R4), DSP_REG(R5), DSP_REG(R6), DSP_REG(R7),

	DSP_REG(N0), DSP_REG(N1), DSP_REG(N2), DSP_REG(N3),
	DSP_REG(N4), DSP_REG(N5), DSP_REG(N6), DSP_REG(N7),

	DSP_REG(M0), DSP_REG(M1), DSP_REG(M2), DSP_REG(M3),
	DSP_REG(M4), DSP_REG(M5), DSP_REG(M6), DSP_REG(M7),

	DSP_REG(SR), DSP_REG(OMR), DSP_REG(SP),
	DSP_REG(SSH), DSP_REG(SSL),
	DSP_REG(LA), DSP_REG(LC),
	{ 0xff, NULL}
};

/**
 * Dump register contents. 
 * This also includes Hatari variables, which we treat as a subset of regs.
 * 
 * Input: "regs\n"
 * 
 * Output: "regs <reg:value>*N\n"
 */
static int RemoteDebug_regs(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int regIdx;
	uint32_t varIndex;
	varIndex = 0;
	const var_addr_t* var;

	static const int regIds[] = {
		REG_D0, REG_D1, REG_D2, REG_D3, REG_D4, REG_D5, REG_D6, REG_D7,
		REG_A0, REG_A1, REG_A2, REG_A3, REG_A4, REG_A5, REG_A6, REG_A7 };
	static const char *regNames[] = {
		"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
		"A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7" };

	send_str(state, "OK");
	send_sep(state);

	// Normal regs
	for (regIdx = 0; regIdx < ARRAY_SIZE(regIds); ++regIdx)
		send_key_value(state, regNames[regIdx], Regs[regIds[regIdx]]);

	if (IsDspActive())
	{
		const RemoteDebugDspReg* pCurrReg = g_remoteDebugDspRegs;
		while (pCurrReg->regName)
		{
			send_key_value(state, pCurrReg->regName, dsp_core.registers[pCurrReg->regId]);
			++pCurrReg;
		}
		send_key_value(state, "D_PC", dsp_core.pc);
	}

	// Special regs
	send_key_value(state, "PC", M68000_GetPC());
	send_key_value(state, "USP", regs.usp);
	send_key_value(state, "ISP", regs.isp);
	send_key_value(state, "SR", M68000_GetSR());
	send_key_value(state, "EX", regs.exception);

	// Variables
	while (Vars_QueryVariable(varIndex, &var))
	{
		uint32_t value;
		value = Vars_GetValue(var);
		send_key_value(state, var->name, value);
		++varIndex;
	}

	if (ConfigureParams.System.nCpuLevel >= 2)
	{
		send_key_value(state, "CAAR", regs.caar);
		send_key_value(state, "CACR", regs.cacr);
		send_key_value(state, "DFC", regs.dfc);
		send_key_value(state, "MSP", regs.msp);
		send_key_value(state, "SFC", regs.sfc);
		send_key_value(state, "VBR", regs.vbr);
	}
	return 0;
}

/**
 * Dump the requested area of ST memory.
 *
 * Input: "mem <start addr:hex> <size in bytes:hex>\n"
 *
 * Output: "mem <address:hex> <size:hexr> <memory as base16 string>\n"
 */

static int RemoteDebug_mem(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg;
	uint32_t memdump_addr = 0;
	uint32_t memdump_count = 0;

	/* For remote debug, only "address" "count" is supported */
	arg = 1;
	if (nArgc >= arg + 2)
	{
		if (!read_hex32_value(psArgs[arg], &memdump_addr))
			return 1;
		++arg;
		if (!read_hex32_value(psArgs[arg], &memdump_count))
			return 1;
		++arg;
	}
	else
	{
		// Not enough args
		return 1;
	}

	send_str(state, "OK");
	send_sep(state);
	send_hex(state, memdump_addr);
	send_sep(state);
	send_hex(state, memdump_count);
	send_sep(state);

	// Need to flush here before we switch to our existing buffer system
	flush_data(state);

	// Send data in blocks of "buffer_size" memory bytes
	// (We don't need a terminator when sending)
	const uint32_t buffer_size = RDB_MEM_BLOCK_SIZE*4;
	char* buffer = malloc(buffer_size);

	uint32_t read_pos = 0;
	uint32_t write_pos = 0;
	uint32_t accum;
	while (read_pos < memdump_count)
	{
		// Accumulate 3 bytes into 24 bits of a u32
		accum = 0;
		for (int i = 0; i < 3; ++i)
		{
			accum <<= 8;
			if (read_pos < memdump_count)
				accum |= STMemory_ReadByte(memdump_addr + read_pos);
			++read_pos;
		}

		// Now write 4 chars out as ASCII uuencode
		buffer[write_pos++] = 32 + ((accum >> 18) & 0x3f);
		buffer[write_pos++] = 32 + ((accum >> 12) & 0x3f);
		buffer[write_pos++] = 32 + ((accum >>  6) & 0x3f);
		buffer[write_pos++] = 32 + ((accum      ) & 0x3f);

		// Flush?
		if (write_pos == RDB_MEM_BLOCK_SIZE*4)
		{
			send(state->AcceptedFD, buffer, write_pos, 0);
			write_pos = 0;
		}
	}

	// Flush remainder
	if (write_pos != 0)
		send(state->AcceptedFD, buffer, write_pos, 0);

	free(buffer);
	return 0;
}

/**
 * Write the requested area of ST memory.
 *
 * Input: "memset <start addr:hex> <hex-data:hex>\n"
 *
 * Output: "OK"/"NG"
 */

static int RemoteDebug_memset(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg;
	uint32_t memset_addr = 0;
	uint32_t memset_end = 0;
	uint32_t memset_count = 0;
	uint8_t valHi;
	uint8_t valLo;

	/* For remote debug, only "address" "count" is supported */
	arg = 1;
	if (nArgc >= arg + 3)
	{
		// Address
		if (!read_hex32_value(psArgs[arg], &memset_addr))
			return 1;
		++arg;
		// Size
		if (!read_hex32_value(psArgs[arg], &memset_count))
			return 1;
		++arg;
	}
	else
	{
		// Not enough args
		return 1;
	}

	memset_end = memset_addr + memset_count;
	uint32_t pos = 0;
	while (memset_addr < memset_end)
	{
		if (!read_hex_char(psArgs[arg][pos], &valHi))
			return 1;
		++pos;
		if (!read_hex_char(psArgs[arg][pos], &valLo))
			return 1;
		++pos;

		STMemory_WriteByte(memset_addr, (valHi << 4) | valLo);
		++memset_addr;
	}
	send_str(state, "OK");
	send_sep(state);
	// Report changed range so tools can decide to update
	send_hex(state, memset_end - memset_count);
	send_sep(state);
	send_hex(state, memset_count);
	return 0;
}

// -----------------------------------------------------------------------------
/* Set a CPU breakpoint at an address. */
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
/* Set a DSP breakpoint at an address. */
static int RemoteDebug_dbp(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg = 1;
	if (nArgc >= arg + 1)
	{
		// Pass to standard simple function
		if (BreakCond_Command(psArgs[arg], true))
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
	send_str(state, "OK");
	send_sep(state);
	send_hex(state, count);
	send_sep(state);

	/* NOTE breakpoint query indices start at 1 */
	for (i = 1; i <= count; ++i)
	{
		bc_breakpoint_query_t query;
		BreakCond_GetCpuBreakpointInfo(i, &query);

		send_str(state, query.expression);
		/* Note this has the ` character to flag the expression end,
		since the expression can contain spaces */
		send_sep(state);
		send_hex(state, query.ccount); send_sep(state);
		send_hex(state, query.hits); send_sep(state);
		send_bool(state, query.once); send_sep(state);
		send_bool(state, query.quiet); send_sep(state);
		send_bool(state, query.trace); send_sep(state);
	}
	return 0;
}

// -----------------------------------------------------------------------------
/* Remove breakpoint number N.
 *
 * Input: "bpdel <breakpoint_index:hex>
 * 
 * NOTE breakpoint IDs start at 1!
*/
static int RemoteDebug_bpdel(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg = 1;
	uint32_t bp_position;
	if (nArgc >= arg + 1)
	{
		if (read_hex32_value(psArgs[arg], &bp_position))
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
	send_str(state, "OK");
	send_sep(state);
	send_hex(state, count);
	send_sep(state);
	
	for (i = 0; i < count; ++i)
	{
		rdb_symbol_t query;
		if (!Symbols_GetCpuSymbol(i, &query))
			break;
		send_str(state, query.name);
		send_sep(state);
		send_hex(state, query.address);
		send_sep(state);
		send_char(state, query.type);
		send_sep(state);
	}
	return 0;
}

// -----------------------------------------------------------------------------
/* "exmask" -- Read or set exception mask
 *
 * Input: exmask
 * Input: exmask <new_mask:hex>
 * 
 *  returns "OK <mask val:hex>"" */
static int RemoteDebug_exmask(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg = 1;
	uint32_t mask;

	if (nArgc == 2)
	{
		// Assumed to set the mask
		if (!read_hex32_value(psArgs[arg], &mask))
			return 1;
		ExceptionDebugMask = mask;
	}

	// Always respond with the mask value, so that setting comes back
	// to the remote debugger
	send_str(state, "OK");
	send_sep(state);
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
		// Repoint all output to any supplied file
		RemoteDebug_OpenDebugOutput(state);
		int cmdRet = DebugUI_ParseConsoleCommand(psArgs[1]);

		/* handle a command that restarts execution */
		if (cmdRet == DEBUGGER_END)
			bRemoteBreakIsActive = false;

		fflush(debugOutput);
		RemoteDebug_CloseDebugOutput(state);

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
#ifdef __WINDOWS__
		strncpy(state->consoleOutputFilename, filename, FILENAME_MAX);
		state->consoleOutputFilename[FILENAME_MAX] = 0; /* ensure terminator */
		return 0;
#else
		FILE* outpipe = fopen(filename, "w");
		if (outpipe)
		{
			// Save this output so that we can pipe to it later
			state->consoleOutputFile = outpipe;
			send_str(state, "OK");
			return 0;
		}
#endif
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* "infoym" returns internal YM/PSG register state */
/* returns "OK" + 16 register values */
static int RemoteDebug_infoym(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	send_str(state, "OK");
	for (int i = 0; i < MAX_PSG_REGISTERS; ++i)
	{
		send_sep(state);
		send_hex(state, PSGRegisters[i]);
	}
	return 0;
}

// -----------------------------------------------------------------------------
/* "profile <int>" Enables/disables CPU profiling. */
/* returns "OK <val>" if successful */
static int RemoteDebug_profile(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int enable;
	if (nArgc == 2)
	{
		enable = atoi(psArgs[1]);
		Profile_CpuEnable(enable);

		send_str(state, "OK");
		send_sep(state);
		send_hex(state, enable);
		return 0;
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* "resetwarm" Trigger system warm reset */
/* returns "OK <val>" if successful */
static int RemoteDebug_resetwarm(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	if (Reset_Warm() == 0)
	{
		send_str(state, "OK");
		return 0;
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* "resetcold" Trigger system cold reset */
/* returns "OK <val>" if successful */
static int RemoteDebug_resetcold(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	if (Reset_Cold() == 0)
	{
		send_str(state, "OK");
		return 0;
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* "ffwd" Control fast-forward on/off */
/* returns "OK <val>" if successful */
static int RemoteDebug_ffwd(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int enable;
	if (nArgc == 2)
	{
		enable = atoi(psArgs[1]);
		ConfigureParams.System.bFastForward = ( enable ? true : false );

		// Insert an out-of-band notification before the response
		RemoteDebug_NotifyState(state);

		send_str(state, "OK");
		send_sep(state);
		send_hex(state, enable);
		return 0;
	}
	return 1;
}

// -----------------------------------------------------------------------------
/* "memfind <start:hex> <count:hex> <stringdata>" Search memory for string */
/* returns "OK <addr>" where addr is the next address found if successful */
static int RemoteDebug_memfind(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	uint32_t find_addr = 0;
	uint32_t find_count = 0;
	uint32_t find_end = 0;
	int readPos = 0;
	RemoteDebugBuffer searchBuffer;
	size_t stringSize;
	uint8_t valHi;
	uint8_t valLo;
	int arg;

	/* For remote debug, only "address" "count" is supported */
	arg = 1;
	if (nArgc >= arg + 2)
	{
		if (!read_hex32_value(psArgs[arg], &find_addr))
			return 1;

		++arg;
		if (!read_hex32_value(psArgs[arg], &find_count))
			return 1;
		++arg;
	}
	else
	{
		// Not enough args
		return 1;
	}

	// Read (hex) search string into a buffer.
	// The string is a set of <mask><value> byte pairs (so we can support case-insensitive)
	RemoteDebugBuffer_Init(&searchBuffer, 30);
	while (1)
	{
		if (!read_hex_char(psArgs[arg][readPos], &valHi))
			break;
		++readPos;
		if (!read_hex_char(psArgs[arg][readPos], &valLo))
			break;
		++readPos;
		char stringVal = (valHi << 4) | valLo;
		RemoteDebugBuffer_Add(&searchBuffer, &stringVal, sizeof(stringVal));
	}
	// Check that we have an even number of bytes in the search string
	if (searchBuffer.write_pos & 1)
		return 2;

	send_str(state, "OK");

	// Then do the search
	stringSize = searchBuffer.write_pos / 2;
	find_end = find_addr + find_count - stringSize;
	while (find_addr < find_end)
	{
		// Primitive string compare.
		// We could do something with a rolling buffer for speed,
		// but it's really not worth it yet.
		bool found = true;
		for (size_t i = 0; i < stringSize; ++i)
		{
			uint8_t mem = STMemory_ReadByte(find_addr + i);
			uint8_t mask = searchBuffer.data[i * 2];
			uint8_t val = searchBuffer.data[i * 2 + 1];

			if ((mem & mask) != val)
			{
				found = false;
				break;
			}
		}

		if (found)
		{
			send_sep(state);
			send_hex(state, find_addr);
			break;
		}
		++find_addr;
	}
	RemoteDebugBuffer_UnInit(&searchBuffer);
	return 0;
}


/**
 * Dump memory from an address to a binary file.
 */
// -----------------------------------------------------------------------------
/* "savebin <start:hex> <count:hex> <filename"  write memory data to binary file */
/* returns OK/NG */
static int RemoteDebug_savebin(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	FILE *fp;
	unsigned char c;
	uint32_t address;
	uint32_t bytes, i = 0;

	if (nArgc != 4)
		return 2;

	if (!read_hex32_value(psArgs[1], &address))
		return 1;

	if (!read_hex32_value(psArgs[2], &bytes))
		return 1;

	if ((fp = fopen(psArgs[3], "wb")) == NULL)
		return 3;

	while (i < bytes)
	{
		c = STMemory_ReadByte(address++);
		fputc(c, fp);
		i++;
	}
	fclose(fp);
	send_str(state, "OK");
	return 0;
}

/**
 * Dump the requested area of DSP memory.
 *
 * Input: "mem <space:char> <start addr:hex> <size in DSP-words:hex>\n"
 *
 * Output: "mem <address:hex> <size:hexr> <memory as base16 string>\n"
 */

static int RemoteDebug_dmem(int nArgc, char *psArgs[], RemoteDebugState* state)
{
	int arg;
	uint32_t result = 0;
	char memspace = 0;
	Uint32 memdump_addr = 0;
	Uint32 memdump_count = 0;
	const char* mem_str = NULL;

	/* For remote debug, only "space" "address" "count" is supported */
	arg = 1;
	if (nArgc >= arg + 3)
	{
		memspace = psArgs[arg][0];
		if (memspace != 'X' && memspace != 'Y' && memspace != 'P')
			return 1;
		++arg;

		if (!read_hex32_value(psArgs[arg], &memdump_addr))
			return 1;
		++arg;
		if (!read_hex32_value(psArgs[arg], &memdump_count))
			return 1;
		++arg;
	}
	else
	{
		// Not enough args
		return 1;
	}

	memdump_addr &= 0xffff;

	flush_data(state);
	send_str(state, "OK");
	send_sep(state);
	send_char(state, memspace);
	send_sep(state);
	send_hex(state, memdump_addr);
	send_sep(state);
	send_hex(state, memdump_count);
	send_sep(state);

	// Send data in blocks of "buffer_size" memory bytes
	// (We don't need a terminator when sending)
	while (memdump_count)
	{
		result = DSP_ReadMemory(memdump_addr, memspace, &mem_str);

		// Now write 3 bytes to 4 chars as ASCII uuencode
		send_char(state, 32 + ((result >> 18) & 0x3f));
		send_char(state, 32 + ((result >> 12) & 0x3f));
		send_char(state, 32 + ((result >>  6) & 0x3f));
		send_char(state, 32 + ((result      ) & 0x3f));
		++memdump_addr;
		--memdump_count;
	}
	return 0;
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
	{ RemoteDebug_status,	"status"	, true		},
	{ RemoteDebug_break,	"break"		, true		},
	{ RemoteDebug_step,		"step"		, true		},
	{ RemoteDebug_dstep,	"dstep"		, true		},
	{ RemoteDebug_run,		"run"		, true		},
	{ RemoteDebug_regs,		"regs"		, true		},
	{ RemoteDebug_mem,		"mem"		, true		},
	{ RemoteDebug_memset,	"memset"	, true		},
	{ RemoteDebug_bp,		"bp"		, false		},
	{ RemoteDebug_dbp,		"dbp"		, false		},
	{ RemoteDebug_bplist,	"bplist"	, true		},
	{ RemoteDebug_bpdel,	"bpdel"		, true		},
	{ RemoteDebug_symlist,	"symlist"	, true		},
	{ RemoteDebug_exmask,	"exmask"	, true		},
	{ RemoteDebug_console,	"console"	, false		},
	{ RemoteDebug_setstd,	"setstd"	, true		},
	{ RemoteDebug_infoym,	"infoym"	, false		},
	{ RemoteDebug_profile,	"profile"	, true		},
	{ RemoteDebug_resetwarm,"resetwarm"	, true		},
	{ RemoteDebug_resetcold,"resetcold"	, true		},
	{ RemoteDebug_ffwd,		"ffwd"		, true		},
	{ RemoteDebug_memfind,	"memfind"	, true		},
	{ RemoteDebug_savebin,	"savebin"	, true		},
	{ RemoteDebug_dmem,		"dmem"		, true		},

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
	RemoteDebugBuffer_Init(&state->input_buf, RDB_CMD_BUFFER_START_SIZE);
	memset(state->cmd_buf, 0, sizeof(state->cmd_buf));
#ifdef __WINDOWS__
	memset(state->consoleOutputFilename, 0, sizeof(state->consoleOutputFilename));
#else
	state->original_stdout = NULL;
	state->original_stderr = NULL;
	state->original_debugOutput = NULL;
	state->consoleOutputFile = NULL;
#endif
	state->sendBufferPos = 0;
}

static void RemoteDebugState_UnInit(RemoteDebugState* state)
{
	if (state->AcceptedFD != -1)
	{
		RDB_CLOSE(state->AcceptedFD);
	}

	if (state->SocketFD != -1)
	{
		RDB_CLOSE(state->SocketFD);
	}

	state->AcceptedFD = -1;
	state->SocketFD = -1;
	RemoteDebugBuffer_UnInit(&state->input_buf);
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
		send_sep(state);
		send_hex(state, REMOTEDEBUG_PROTOCOL_ID);
		send_term(state);
		flush_data(state);

		// New connection, so do an initial report.
		RemoteDebug_NotifyConfig(state);
		RemoteDebug_NotifyState(state);
		RemoteDebug_NotifySymbols(state);
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
		// Scan for a complete command by looking for the terminator
		char* endptr = memchr(state->input_buf.data, 0, state->input_buf.write_pos);
		if (!endptr)
			break;

		size_t cmd_length = endptr- state->input_buf.data + 1;

		// Process this command
		cmd_ret = RemoteDebug_Parse(state->input_buf.data, state);

		if (cmd_ret != 0)
		{
			// Return an error if something failed
			// and give the error number
			send_str(state, "NG");
			send_sep(state);
			send_hex(state, cmd_ret);
		}
		send_term(state);

		// Copy extra bytes to the start
		RemoteDebugBuffer_RemoveStart(&state->input_buf, cmd_length);
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
	remaining = sizeof(state->cmd_buf);
	int bytes = recv(state->AcceptedFD, 
		state->cmd_buf,
		remaining,
		0);
	if (bytes > 0)
	{
		// New data.
		// Add to the resizeable buffer
		RemoteDebugBuffer_Add(&state->input_buf, state->cmd_buf, bytes);
		// Check for completed commands
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
		RemoteDebug_NotifyProfile(state);
		flush_data(state);
	}

	RemoteDebug_HardwareSync();

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
	printf("Remote Debug Listening on port %d, protocol %x\n", RDB_PORT, REMOTEDEBUG_PROTOCOL_ID);
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
		remaining = sizeof(state->cmd_buf);
		
#if HAVE_UNIX_DOMAIN_SOCKETS
		int bytes = recv(state->AcceptedFD, 
			state->cmd_buf,
			remaining,
			MSG_DONTWAIT);
#endif
#if HAVE_WINSOCK_SOCKETS
		int bytes = recv(state->AcceptedFD, 
			state->cmd_buf,
			remaining,
			0);
#endif

		if (bytes > 0)
		{
			// New data.
			// Add to the resizeable buffer
			RemoteDebugBuffer_Add(&state->input_buf, state->cmd_buf, bytes);
			// Check for completed commands
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
	Symbols_RegisterCpuChangedCallback(RemoteDebug_SymbolsChanged);
}

void RemoteDebug_UnInit()
{
	printf("Stopping remote debug\n");
	Symbols_RegisterCpuChangedCallback(NULL);
	DebugUI_RegisterRemoteDebug(NULL);

	RemoteDebugState_UnInit(&g_rdbState);
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

void RemoteDebug_SymbolsChanged()
{
	RemoteDebug_NotifySymbols(&g_rdbState);
}