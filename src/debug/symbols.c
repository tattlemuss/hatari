/*
 * Hatari - symbols.c
 * 
 * Copyright (C) 2010-2024 by Eero Tamminen
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 * 
 * symbols.c - Hatari debugger symbol/address handling; parsing, sorting,
 * matching, TAB completion support etc.
 * 
 * Symbol/address information is read either from:
 * - A program file's symbol table (in DRI/GST, a.out, or ELF format), or
 * - ASCII file which contents are subset of "nm" output i.e. composed of
 *   a hexadecimal addresses followed by a space, letter indicating symbol
 *   type (T = text/code, D = data, B = BSS), space and the symbol name.
 *   Empty lines and lines starting with '#' are ignored.  It's AHCC SYM
 *   output compatible.
 */
const char Symbols_fileid[] = "Hatari symbols.c";

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "config.h"

#if HAVE_LIBREADLINE
# include <readline/readline.h>
#else
# define rl_filename_completion_function(x,y) NULL
#endif

#include "main.h"
#include "file.h"
#include "options.h"
#include "symbols.h"
#include "debugui.h"
#include "debug_priv.h"
#include "debugInfo.h"
#include "evaluate.h"
#include "configuration.h"
#include "a.out.h"
#include "maccess.h"

#include "symbols-common.c"

/* how many characters the symbol name can have.
 *
 * While DRI/GST symbols are at max only couple of dozen
 * chars long, C++/a.out symbols can be almost of any size.
 */
#define MAX_SYM_SIZE 1024
#define MAX_SYM_SIZE_S "1024"

/* TODO: add symbol name/address file names to configuration? */
static symbol_list_t *CpuSymbolsList;
static symbol_list_t *DspSymbolsList;

/* path for last loaded program (through GEMDOS HD emulation) */
static char *CurrentProgramPath;
/* prevent repeated failing on every debugger invocation */
static bool AutoLoadFailed;

/* Remote debug code: debugging callback to inform of symbol table change */
static Symbols_ChangedCallback CpuSymbolsChangedCallback = NULL;

typedef enum {
	SYMBOLS_FOR_NONE,
	SYMBOLS_FOR_USER,
	/* autoload facilities */
	SYMBOLS_FOR_TOS,
	SYMBOLS_FOR_PROGRAM,
} symbols_for_t;

/* what triggered current CPU symbols to be loaded */
static symbols_for_t CpuSymbolsAreFor = SYMBOLS_FOR_NONE;


/**
 * Load symbols of given type and the symbol address addresses from
 * the given ASCII file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* symbols_load_ascii(FILE *fp, uint32_t *offsets, uint32_t maxaddr,
					 symtype_t gettype, const symbol_opts_t *opts)
{
	symbol_list_t *list;
	char symchar, name[MAX_SYM_SIZE+1];
	char *buf, buffer[MAX_SYM_SIZE+64];
	int count, line, symbols;
	uint32_t address, offset;
	ignore_counts_t ignore;
	symtype_t symtype;

	/* count content lines */
	line = symbols = 0;
	while (fgets(buffer, sizeof(buffer), fp)) {
		line++;

		/* skip comments (AHCC SYM file comments start with '*') */
		if (*buffer == '#' || *buffer == '*') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace((unsigned char)*buf); buf++);
		if (!*buf) {
			continue;
		}
		if (!isxdigit((unsigned char)*buf)) {
			fprintf(stderr, "ERROR: line %d doesn't start with an address.\n", line);
			return NULL;
		}
		symbols++;
	}
	if (!symbols) {
		fprintf(stderr, "ERROR: no symbols.\n");
		return NULL;
	}

	fseek(fp, 0, SEEK_SET);

	/* allocate space for symbol list & names */
	if (!(list = symbol_list_alloc(symbols))) {
		return NULL;
	}

	count = 0;
	memset(&ignore, 0, sizeof(ignore));

	/* read symbols */
	for (line = 1; fgets(buffer, sizeof(buffer), fp); line++) {
		/* skip comments (AHCC SYM file comments start with '*') */
		if (*buffer == '#' || *buffer == '*') {
			continue;
		}
		/* skip empty lines */
		for (buf = buffer; isspace((unsigned char)*buf); buf++);
		if (!*buf) {
			continue;
		}
		/* file not modified in meanwhile? */
		assert(count < symbols);
		/* C++ symbols can contain almost any characters */
		if (sscanf(buffer, "%x %c %"MAX_SYM_SIZE_S"[^$?@;\n]\n", &address, &symchar, name) != 3) {
			fprintf(stderr, "WARNING: syntax error on line %d, skipping.\n", line);
			continue;
		}
		switch (toupper((unsigned char)symchar)) {
		case 'T':
			symtype = SYMTYPE_TEXT;
			offset = offsets[0];
			break;
		case 'W':	/* ELF 'nm' code symbol type, weak */
			symtype = SYMTYPE_WEAK;
			offset = offsets[0];
			break;
		case 'O':	/* AHCC type for _StkSize etc */
		case 'V':	/* ELF 'nm' data symbol type, weak */
		case 'R':	/* ELF 'nm' data symbol type, read only */
		case 'D':
			symtype = SYMTYPE_DATA;
			offset = offsets[1];
			break;
		case 'B':
			symtype = SYMTYPE_BSS;
			offset = offsets[2];
			break;
		case 'A':
			/* absolute address or arbitrary constant value */
			symtype = SYMTYPE_ABS;
			offset = 0;
			break;
		default:
			fprintf(stderr, "WARNING: unrecognized symbol type '%c' on line %d, skipping.\n", symchar, line);
			ignore.invalid++;
			continue;
		}
		if (!(gettype & symtype)) {
			continue;
		}
		address += offset;
		if (address > maxaddr && symtype != SYMTYPE_ABS) {
			fprintf(stderr, "WARNING: invalid address 0x%x on line %d, skipping.\n", address, line);
			ignore.invalid++;
			continue;
		}
		/* whether to ignore symbol based on options and its name & type */
		if (ignore_symbol(name, symtype, opts, &ignore)) {
			continue;
		}
		list->names[count].address = address;
		list->names[count].type = symtype;
		list->names[count].name = strdup(name);
		list->names[count].name_allocated = true;
		assert(list->names[count].name);
		count++;
	}
	show_ignored(&ignore);
	list->symbols = symbols;
	list->namecount = count;
	return list;
}

/**
 * Return true if symbol name has (C++) data symbol prefix
 */
static bool is_cpp_data_symbol(const char *name)
{
	static const char *cpp_data[] = {
		"typeinfo ",
		"vtable ",
		"VTT "
	};
	int i;
	for (i = 0; i < ARRAY_SIZE(cpp_data); i++) {
		size_t len = strlen(cpp_data[i]);
		if (strncmp(name, cpp_data[i], len) == 0) {
			return true;
		}
	}
	return false;
}

/**
 * (C++) compiler can put certain data members to text section, and
 * some of the weak (C++) symbols are for data. For C++, these can be
 * recognized by their name.  This changes their type to data, to
 * speed up text symbol searches in profiler.
 */
static int fix_symbol_types(symbol_list_t* list)
{
	symbol_t *sym = list->names;
	int i, count, changed = 0;

	count = list->namecount;
	for (i = 0; i < count; i++) {
		if (!(sym[i].type & SYMTYPE_CODE)) {
			continue;
		}
		if (is_cpp_data_symbol(sym[i].name)) {
			sym[i].type = SYMTYPE_DATA;
			changed++;
		}
		/* TODO: add check also for C++ data member
		 * names, similar to profiler post-processor
		 * (requires using regex)?
		 */
	}
	return changed;
}

/**
 * Separate code symbols from other symbols in address list.
 */
static void symbols_split_addresses(symbol_list_t* list)
{
	symbol_t *sym = list->addresses;
	uint32_t prev = 0;
	int i;

	for (i = 0; i < list->namecount; i++) {
		if (sym[i].type & ~SYMTYPE_CODE) {
			break;
		}
		if (sym[i].address < prev) {
			char stype = symbol_char(sym[i].type);
			fprintf(stderr, "INTERNAL ERROR: %c symbol %d/%d ('%s') at %x < %x (prev addr)\n",
				stype, i, list->namecount, sym[i].name, sym[i].address, prev);
			exit(1);
		}
		prev = sym[i].address;
	}
	list->codecount = i;
	list->datacount = list->namecount - i;
}

/**
 * Set sections to match running process by adding TEXT/DATA/BSS
 * start addresses to section offsets and ends, and return true if
 * results match it.
 */
static bool update_sections(prg_section_t *sections)
{
	/* offsets & max sizes for running program TEXT/DATA/BSS section symbols */
	uint32_t start = DebugInfo_GetTEXT();
	if (!start) {
		fprintf(stderr, "ERROR: no valid program basepage!\n");
		return false;
	}
	sections[0].offset = start;
	sections[0].end += start;
	if (DebugInfo_GetTEXTEnd() != sections[0].end) {
		fprintf(stderr, "ERROR: given program TEXT section size differs from one in RAM!\n");
		return false;
	}

	start = DebugInfo_GetDATA();
	sections[1].offset = start;
	if (sections[1].offset != sections[0].end) {
		fprintf(stderr, "WARNING: DATA start doesn't match TEXT start + size!\n");
	}
	sections[1].end += start;

	start = DebugInfo_GetBSS();
	sections[2].offset = start;
	if (sections[2].offset != sections[1].end) {
		fprintf(stderr, "WARNING: BSS start doesn't match DATA start + size!\n");
	}
	sections[2].end += start;

	return true;
}

/**
 * Load symbols of given type and the symbol address addresses from
 * the given file and add given offsets to the addresses.
 * Return symbols list or NULL for failure.
 */
static symbol_list_t* Symbols_Load(const char *filename, uint32_t *offsets, uint32_t maxaddr, symtype_t gettype)
{
	symbol_list_t *list;
	symbol_opts_t opts;
	int changed, dups;
	FILE *fp;

	if (!File_Exists(filename)) {
		fprintf(stderr, "ERROR: file '%s' doesn't exist or isn't readable!\n", filename);
		return NULL;
	}
	memset(&opts, 0, sizeof(opts));
	opts.no_gccint = true;
	opts.no_local = true;
	opts.no_dups = true;

	if (Opt_IsAtariProgram(filename)) {
		const char *last = CurrentProgramPath;
		if (!last) {
			/* "pc=text" breakpoint used as point for loading program symbols gives false hits during bootup */
			fprintf(stderr, "WARNING: no program loaded yet (through GEMDOS HD emu)!\n");
		} else if (strcmp(last, filename) != 0) {
			fprintf(stderr, "WARNING: given program doesn't match last program executed by GEMDOS HD emulation:\n\t%s\n", last);
		}
		fprintf(stderr, "Reading symbols from program '%s' symbol table...\n", filename);
		fp = fopen(filename, "rb");
		list = symbols_load_binary(fp, &opts, update_sections);
	} else {
		fprintf(stderr, "Reading 'nm' style ASCII symbols from '%s'...\n", filename);
		fp = fopen(filename, "r");
		list = symbols_load_ascii(fp, offsets, maxaddr, gettype, &opts);
	}
	fclose(fp);

	if (!list) {
		fprintf(stderr, "ERROR: reading symbols from '%s' failed!\n", filename);
		return NULL;
	}

	if (!list->namecount) {
		fprintf(stderr, "ERROR: no valid symbols in '%s', loading failed!\n", filename);
		symbol_list_free(list);
		return NULL;
	}

	if ((changed = fix_symbol_types(list))) {
		fprintf(stderr, "Corrected type for %d symbols (text->data).\n", changed);
	}

	/* first sort symbols by address, _with_ code symbols being first */
	qsort(list->names, list->namecount, sizeof(symbol_t), symbols_by_address);

	/* remove symbols with duplicate addresses? */
	if (opts.no_dups) {
		if ((dups = symbols_trim_names(list))) {
			fprintf(stderr, "Removed %d symbols in same addresses as other symbols.\n", dups);
		}
	}

	/* copy name list to address list */
	list->addresses = malloc(list->namecount * sizeof(symbol_t));
	assert(list->addresses);
	memcpy(list->addresses, list->names, list->namecount * sizeof(symbol_t));

	/* "split" address list to code and other symbols */
	symbols_split_addresses(list);

	/* finally, sort name list by names */
	qsort(list->names, list->namecount, sizeof(symbol_t), symbols_by_name);

	/* skip more verbose output when symbols are auto-loaded */
	if (ConfigureParams.Debugger.bSymbolsAutoLoad) {
		fprintf(stderr, "Skipping detailed duplicate symbols reporting when autoload is enabled.\n");
	} else {
		/* check for duplicate addresses? */
		if (!opts.no_dups) {
			if ((dups = symbols_check_addresses(list->addresses, list->namecount))) {
			fprintf(stderr, "%d symbols in same addresses as other symbols.\n", dups);
			}
		}

		/* report duplicate names */
		if ((dups = symbols_check_names(list->names, list->namecount))) {
			fprintf(stderr, "%d symbols having multiple addresses for the same name.\n"
				"Symbol expansion will match only one of the addresses for them!\n",
				dups);
		}
	}

	fprintf(stderr, "Loaded %d symbols (%d for code) from '%s'.\n",
		list->namecount, list->codecount, filename);
	return list;
}


/**
 * Free read symbols.
 */
static void Symbols_Free(symbol_list_t* list)
{
	symbol_list_free(list);
}

/**
 * Free all symbols (at exit).
 */
void Symbols_FreeAll(void)
{
	symbol_list_free(CpuSymbolsList);
	symbol_list_free(DspSymbolsList);
}

/**
 * Change current CPU symbols to new ones with given type,
 * free old ones
 */
static void Symbols_UpdateCpu(symbol_list_t* list, symbols_for_t symfor)
{
	if (CpuSymbolsList) {
		symbol_list_free(CpuSymbolsList);
	}
	CpuSymbolsList = list;
	CpuSymbolsAreFor = symfor;
	if (CpuSymbolsChangedCallback)
		CpuSymbolsChangedCallback();
}

/**
 * Change current DSP symbols to new ones, free old ones
 */
static void Symbols_UpdateDsp(symbol_list_t* list)
{
	if (DspSymbolsList) {
		symbol_list_free(DspSymbolsList);
	}
	DspSymbolsList = list;
}

/* ---------------- symbol name completion support ------------------ */

/**
 * Helper for symbol name completion and finding their addresses.
 * STATE = 0 -> different text from previous one.
 * Return (copy of) next name or NULL if no matches.
 */
static char* Symbols_MatchByName(symbol_list_t* list, symtype_t symtype, const char *text, int state)
{
	static int i, len;
	const symbol_t *entry;
	
	if (!list) {
		return NULL;
	}

	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}

	/* next match */
	entry = list->names;
	while (i < list->namecount) {
		if ((entry[i].type & symtype) &&
		    strncmp(entry[i].name, text, len) == 0) {
			return strdup(entry[i++].name);
		} else {
			i++;
		}
	}
	return NULL;
}

/**
 * Readline match callbacks for CPU symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char* Symbols_MatchCpuAddress(const char *text, int state)
{
	return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_ALL, text, state);
}
char* Symbols_MatchCpuCodeAddress(const char *text, int state)
{
	if (ConfigureParams.Debugger.bMatchAllSymbols) {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_ALL, text, state);
	} else {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_CODE, text, state);
	}
}
char* Symbols_MatchCpuDataAddress(const char *text, int state)
{
	if (ConfigureParams.Debugger.bMatchAllSymbols) {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_ALL, text, state);
	} else {
		return Symbols_MatchByName(CpuSymbolsList, SYMTYPE_DATA|SYMTYPE_BSS, text, state);
	}
}

/**
 * Readline match callback to list matching CPU symbols & file names.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Symbols_MatchCpuAddrFile(const char *text, int state)
{
	char *ret = Symbols_MatchCpuAddress(text, state);
	if (ret) {
		return ret;
	}
	return rl_filename_completion_function(text, state);
}

/**
 * Readline match callback for DSP symbol name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char* Symbols_MatchDspAddress(const char *text, int state)
{
	return Symbols_MatchByName(DspSymbolsList, SYMTYPE_ALL, text, state);
}
char* Symbols_MatchDspCodeAddress(const char *text, int state)
{
	return Symbols_MatchByName(DspSymbolsList, SYMTYPE_CODE, text, state);
}
char* Symbols_MatchDspDataAddress(const char *text, int state)
{
	return Symbols_MatchByName(DspSymbolsList, SYMTYPE_DATA|SYMTYPE_BSS, text, state);
}


/* ---------------- symbol name -> address search ------------------ */

/**
 * Binary search symbol of given type by name.
 * Return symbol if name matches, zero otherwise.
 */
static const symbol_t* Symbols_SearchByName(symbol_t* entries, int count, symtype_t symtype, const char *name)
{
	/* left, right, middle */
        int l, r, m, dir;

	/* bisect */
	l = 0;
	r = count - 1;
	do {
		m = (l+r) >> 1;
		dir = strcmp(entries[m].name, name);
		if (dir == 0 && (entries[m].type & symtype)) {
			return &(entries[m]);
		}
		if (dir > 0) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return NULL;
}

/**
 * Set given symbol's address to variable and return true if one
 * was found from given list.
 */
static bool Symbols_GetAddress(symbol_list_t* list, symtype_t symtype, const char *name, uint32_t *addr)
{
	const symbol_t *entry;
	if (!(list && list->names)) {
		return false;
	}
	entry = Symbols_SearchByName(list->names, list->namecount, symtype, name);
	if (entry) {
		*addr = entry->address;
		return true;
	}
	return false;
}
bool Symbols_GetCpuAddress(symtype_t symtype, const char *name, uint32_t *addr)
{
	return Symbols_GetAddress(CpuSymbolsList, symtype, name, addr);
}
bool Symbols_GetDspAddress(symtype_t symtype, const char *name, uint32_t *addr)
{
	return Symbols_GetAddress(DspSymbolsList, symtype, name, addr);
}


/* ---------------- symbol address -> name search ------------------ */

/**
 * Binary search code symbol by address in given sorted list.
 * Return index for symbol which address matches or precedes
 * the given one.
 */
static int Symbols_SearchBeforeAddress(symbol_t* entries, int count, uint32_t addr)
{
	/* left, right, middle */
        int l, r, m;
	uint32_t curr;

	/* bisect */
	l = 0;
	r = count - 1;
	do {
		m = (l+r) >> 1;
		curr = entries[m].address;
		if (curr == addr) {
			return m;
		}
		if (curr > addr) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return r;
}

static const char* Symbols_GetBeforeAddress(symbol_list_t *list, uint32_t *addr)
{
	if (!(list && list->addresses)) {
		return NULL;
	}
	int i = Symbols_SearchBeforeAddress(list->addresses, list->codecount, *addr);
	if (i >= 0) {
		*addr = list->addresses[i].address;
		return list->addresses[i].name;
	}
	return NULL;
}
const char* Symbols_GetBeforeCpuAddress(uint32_t *addr)
{
	return Symbols_GetBeforeAddress(CpuSymbolsList, addr);
}
const char* Symbols_GetBeforeDspAddress(uint32_t *addr)
{
	return Symbols_GetBeforeAddress(DspSymbolsList, addr);
}

/**
 * Binary search symbol by address in given sorted list.
 * Return symbol index if address matches, -1 otherwise.
 *
 * Performance critical, called on every instruction
 * when profiling is enabled.
 */
static int Symbols_SearchByAddress(symbol_t* entries, int count, uint32_t addr)
{
	/* left, right, middle */
        int l, r, m;
	uint32_t curr;

	/* bisect */
	l = 0;
	r = count - 1;
	do {
		m = (l+r) >> 1;
		curr = entries[m].address;
		if (curr == addr) {
			return m;
		}
		if (curr > addr) {
			r = m-1;
		} else {
			l = m+1;
		}
	} while (l <= r);
	return -1;
}

/**
 * Search symbol in given list by type & address.
 * Return symbol name if there's a match, NULL otherwise.
 * Code symbols will be matched before other symbol types.
 * Returned name is valid only until next Symbols_* function call.
 */
static const char* Symbols_GetByAddress(symbol_list_t* list, uint32_t addr, symtype_t type)
{
	if (!(list && list->addresses)) {
		return NULL;
	}
	if (type & SYMTYPE_CODE) {
		int i = Symbols_SearchByAddress(list->addresses, list->codecount, addr);
		if (i >= 0) {
			return list->addresses[i].name;
		}
	}
	if (type & ~SYMTYPE_CODE) {
		int i = Symbols_SearchByAddress(list->addresses + list->codecount, list->datacount, addr);
		if (i >= 0) {
			return list->addresses[list->codecount + i].name;
		}
	}
	return NULL;
}
const char* Symbols_GetByCpuAddress(uint32_t addr, symtype_t type)
{
	return Symbols_GetByAddress(CpuSymbolsList, addr, type);
}
const char* Symbols_GetByDspAddress(uint32_t addr, symtype_t type)
{
	return Symbols_GetByAddress(DspSymbolsList, addr, type);
}

/**
 * Search given list for code symbol by address.
 * Return symbol index if address matches, -1 otherwise.
 */
static int Symbols_GetCodeIndex(symbol_list_t* list, uint32_t addr)
{
	if (!list) {
		return -1;
	}
	return Symbols_SearchByAddress(list->addresses, list->codecount, addr);
}
int Symbols_GetCpuCodeIndex(uint32_t addr)
{
	return Symbols_GetCodeIndex(CpuSymbolsList, addr);
}
int Symbols_GetDspCodeIndex(uint32_t addr)
{
	return Symbols_GetCodeIndex(DspSymbolsList, addr);
}

/**
 * Return how many TEXT symbols are loaded/available
 */
int Symbols_CpuCodeCount(void)
{
	return (CpuSymbolsList ? CpuSymbolsList->codecount : 0);
}
int Symbols_DspCodeCount(void)
{
	return (DspSymbolsList ? DspSymbolsList->codecount : 0);
}

/* ---------------- symbol showing ------------------ */

/**
 * Show symbols matching (optional) 'find' string from given list,
 * using paging.
 */
static void Symbols_Show(symbol_list_t* list, const char *sortcmd, const char *find)
{
	symbol_t *entry, *entries;
	const char *symtype, *sorttype;
	int i, row, rows, count, matches;
	char symchar;
	
	if (!list) {
		fprintf(stderr, "No symbols!\n");
		return;
	}

	if (strcmp("code", sortcmd) == 0) {
		sorttype = "address";
		entries = list->addresses;
		count = list->codecount;
		symtype = " TEXT/WEAK";
	} else if (strcmp("data", sortcmd) == 0) {
		sorttype = "address";
		entries = list->addresses + list->codecount;
		count = list->datacount;
		symtype = " DATA/BSS/ABS";
	} else {
		sorttype = "name";
		entries = list->names;
		count = list->namecount;
		symtype = "";
	}
	rows = DebugUI_GetPageLines(ConfigureParams.Debugger.nSymbolLines, 20);
	row = matches = 0;

	for (entry = entries, i = 0; i < count; i++, entry++) {
		if (find && !strstr(entry->name, find)) {
			continue;
		}
		matches++;

		symchar = symbol_char(entry->type);
		fprintf(stderr, "0x%08x %c %s\n",
			entry->address, symchar, entry->name);

		row++;
		if (row >= rows) {
			row = 0;
			if (DebugUI_DoQuitQuery("symbol list"))
				break;
		}
	}
	fprintf(stderr, "%d %s%s symbols (of %d) sorted by %s.\n",
		matches, (list == CpuSymbolsList ? "CPU" : "DSP"),
		symtype, count, sorttype);
}

/* ---------------- binary load handling ------------------ */

/**
 * If autoloading is enabled and program symbols are present,
 * remove them along with program path.
 *
 * Called on GEMDOS reset and when program terminates
 * (unless terminated with Ptermres()).
 */
void Symbols_RemoveCurrentProgram(void)
{
	if (CurrentProgramPath) {
		free(CurrentProgramPath);
		CurrentProgramPath = NULL;

		if (CpuSymbolsList && CpuSymbolsAreFor == SYMBOLS_FOR_PROGRAM &&
		    ConfigureParams.Debugger.bSymbolsAutoLoad) {
			Symbols_Free(CpuSymbolsList);
			fprintf(stderr, "Program exit, removing its symbols.\n");
			CpuSymbolsAreFor = SYMBOLS_FOR_NONE;
			CpuSymbolsList = NULL;
			if (CpuSymbolsChangedCallback)
				CpuSymbolsChangedCallback();
		}
	}
	AutoLoadFailed = false;
}

/**
 * Call Symbols_RemoveCurrentProgram() and
 * set last opened program path.
 *
 * Called on first Fopen() after Pexec().
 */
void Symbols_ChangeCurrentProgram(const char *path)
{
	if (Opt_IsAtariProgram(path)) {
		Symbols_RemoveCurrentProgram();
		CurrentProgramPath = strdup(path);
	}
}

/*
 * Show currently set program path
 */
void Symbols_ShowCurrentProgramPath(FILE *fp)
{
	if (CurrentProgramPath) {
		fprintf(fp, "Current program path: %s\n", CurrentProgramPath);
	} else {
		fputs("No program has been loaded (through GEMDOS HD).\n", fp);
	}
}

/**
 * Autoload helper.  Given the base file name with .XXX extension,
 * if there's another file with .sym extension, load symbols from it,
 * and return them.
 *
 * Assumes all (relevant) sections use the same load address.
 */
static symbol_list_t *loadSymFile(const char *path, symtype_t symtype,
				  uint32_t loadaddr, uint32_t maxaddr)
{
	char symfile[PATH_MAX];
	size_t len = strlen(path);

	if (len <= 3 || path[len-4] != '.' || len >= sizeof(symfile)) {
		return NULL;
	}
	strcpy(symfile, path);
	strcpy(symfile + len - 3, "sym");

	if (!File_Exists(symfile)) {
		return NULL;
	}
	fprintf(stderr, "Loading sym file: %s\n", symfile);

	uint32_t offsets[3] = { loadaddr, loadaddr, loadaddr };
	return Symbols_Load(symfile, offsets, maxaddr, symtype);
}

/**
 * Load symbols for last opened program when symbol autoloading is enabled.
 *
 * If there's file with same name as the program, but with '.sym'
 * extension, that overrides / is loaded instead of the symbol table
 * in the program.
 *
 * Called when debugger is invoked.
 */
void Symbols_LoadCurrentProgram(void)
{
	if (!ConfigureParams.Debugger.bSymbolsAutoLoad) {
		return;
	}
	/* program path missing or previous load failed? */
	if (!CurrentProgramPath || AutoLoadFailed) {
		return;
	}
	/* do not override manually loaded symbols, or
	 * load new symbols if previous program did not terminate.
	 * Autoloaded TOS symbols could be overridden though
	 */
	if (CpuSymbolsList && CpuSymbolsAreFor != SYMBOLS_FOR_TOS) {
		return;
	}

	uint32_t loadaddr = DebugInfo_GetTEXT();
	uint32_t maxaddr = DebugInfo_GetTEXTEnd();
	symbol_list_t *symbols;

	symbols = loadSymFile(CurrentProgramPath, SYMTYPE_CODE, loadaddr, maxaddr);
	if (symbols) {
		fprintf(stderr, "Symbols override loaded for: %s\n", CurrentProgramPath);
	} else {
		symbols = Symbols_Load(CurrentProgramPath, NULL, 0,
				       SYMTYPE_CODE);
	}
	if (!symbols) {
		AutoLoadFailed = true;
		return;
	}

	Symbols_UpdateCpu(symbols, SYMBOLS_FOR_PROGRAM);
	AutoLoadFailed = false;
}

/**
 * If autoloading enabled and no symbols are present, load symbols
 * for "<tos>.img" file from "<tos>.sym" file, if one exists.
 *
 * Called whenever TOS is loaded.
 */
void Symbols_LoadTOS(const char *path, uint32_t maxaddr)
{
	if (!ConfigureParams.Debugger.bSymbolsAutoLoad) {
		return;
	}
	/* do not override manually loaded symbols */
	if (CpuSymbolsList && CpuSymbolsAreFor == SYMBOLS_FOR_USER) {
		return;
	}
	symbol_list_t *symbols;
	symbols = loadSymFile(path, SYMTYPE_ALL, 0, maxaddr);
	if (symbols) {
		fprintf(stderr, "Loaded symbols for TOS: %s\n", path);
		Symbols_UpdateCpu(symbols, SYMBOLS_FOR_TOS);
	}
}

/* ---------------- command parsing ------------------ */

/**
 * Readline match callback for CPU symbols command.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Symbols_MatchCpuCommand(const char *text, int state)
{
	static const char* subs[] = {
		"autoload", "code", "data", "free", "match", "name", "prg"
	};
	char *ret = DebugUI_MatchHelper(subs, ARRAY_SIZE(subs), text, state);
	if (ret) {
		return ret;
	}
	if ((ret = Symbols_MatchCpuAddress(text, state))) {
		return ret;
	}
	return rl_filename_completion_function(text, state);
}

/**
 * Readline match callback to list DSP symbols command.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Symbols_MatchDspCommand(const char *text, int state)
{
	static const char* subs[] = {
		"code", "data", "free", "match", "name"
	};
	char *ret = DebugUI_MatchHelper(subs, ARRAY_SIZE(subs), text, state);
	if (ret) {
		return ret;
	}
	if ((ret = Symbols_MatchDspAddress(text, state))) {
		return ret;
	}
	return rl_filename_completion_function(text, state);
}

const char Symbols_Description[] =
	"<code|data|name> [find] -- list symbols containing 'find'\n"
	"\tsymbols <prg|free> -- load/free symbols\n"
	"\t        <filename> [<T offset> [<D offset> <B offset>]]\n"
	"\tsymbols <autoload|match> -- toggle symbol options\n"
	"\n"
	"\t'name' command lists the currently loaded symbols, sorted by name.\n"
	"\t'code' and 'data' commands list them sorted by address; 'code' lists\n"
	"\tonly TEXT/WEAK symbols, 'data' lists DATA/BSS/ABS symbols. If 'find'\n"
	"\tis given, only symbols with that substring are listed.\n"
	"\n"
	"\tBy default, symbols are loaded from the currently executing program's\n"
	"\tbinary when entering the debugger, IF program is started through\n"
	"\tGEMDOS HD, and they're freed when that program terminates.\n"
	"\n"
	"\tThat corresponds to 'prg' command which loads (DRI/GST or a.out\n"
	"\tformat) symbol table from the last program executed through\n"
	"\tthe GEMDOS HD emulation.\n"
	"\n"
	"\t'free' command removes the loaded symbols.\n"
	"\n"
	"\tIf program lacks symbols, or it's not run through the GEMDOS HD\n"
	"\temulation, user can ask symbols to be loaded from a file that's\n"
	"\tan unstripped version of the binary. Or from an ASCII symbols file\n"
	"\tproduced by the 'nm' and (Hatari) 'gst2ascii' tools.\n"
	"\n"
	"\tWith ASCII symbols files, given non-zero offset(s) are added to\n"
	"\tthe text (T), data (D) and BSS (B) symbols.  Typically one uses\n"
	"\tTEXT variable, sometimes also DATA & BSS, variables for this.\n"
	"\n"
	"\t'autoload [on|off]' command toggle/set whether debugger will load\n"
	"\tsymbols for currently executing (GEMDOS HD) program automatically\n"
	"\ton entering the debugger (i.e. replace earlier loaded symbols),\n"
	"\tand free them when program terminates.  It needs to be disabled\n"
	"\tto debug memory-resident programs used by other programs.\n"
	"\n"
	"\t'match' command toggles whether TAB completion matches all symbols,\n"
	"\tor only symbol types that should be relevant for given command.";


/**
 * Handle debugger 'symbols' command and its arguments
 */
int Symbols_Command(int nArgc, char *psArgs[])
{
	enum { TYPE_CPU, TYPE_DSP } listtype;
	uint32_t offsets[3], maxaddr;
	symbol_list_t *list;
	const char *file;
	int i;

	if (strcmp("dspsymbols", psArgs[0]) == 0) {
		listtype = TYPE_DSP;
		maxaddr = 0xFFFF;
	} else {
		listtype = TYPE_CPU;
		if ( ConfigureParams.System.bAddressSpace24 )
			maxaddr = 0x00FFFFFF;
		else
			maxaddr = 0xFFFFFFFF;
	}
	if (nArgc < 2) {
		file = "name";
	} else {
		file = psArgs[1];
	}

	/* set whether to autoload symbols on program start and
	 * discard them when program terminates with GEMDOS HD,
	 * or whether they need to be loaded manually.
	 */
	if (listtype == TYPE_CPU && strcmp(file, "autoload") == 0) {
		bool value;
		if (nArgc < 3) {
			value = !ConfigureParams.Debugger.bSymbolsAutoLoad;
		} else if (strcmp(psArgs[2], "on") == 0) {
			value = true;
		} else if (strcmp(psArgs[2], "off") == 0) {
			value = false;
		} else {
			DebugUI_PrintCmdHelp(psArgs[0]);
			return DEBUGGER_CMDDONE;
		}
		fprintf(stderr, "Program symbols auto-loading AND freeing (with GEMDOS HD) is %s\n",
		        value ? "ENABLED." : "DISABLED!");
		ConfigureParams.Debugger.bSymbolsAutoLoad = value;
		return DEBUGGER_CMDDONE;
	}

	/* toggle whether all or only specific symbols types get TAB completed? */
	if (strcmp(file, "match") == 0) {
		ConfigureParams.Debugger.bMatchAllSymbols = !ConfigureParams.Debugger.bMatchAllSymbols;
		if (ConfigureParams.Debugger.bMatchAllSymbols) {
			fprintf(stderr, "Matching all symbols types.\n");
		} else {
			fprintf(stderr, "Matching only symbols (most) relevant for given command.\n");
		}
		return DEBUGGER_CMDDONE;
	}

	/* show requested symbol types in requested order? */
	if (strcmp(file, "name") == 0 || strcmp(file, "code") == 0 || strcmp(file, "data") == 0) {
		const char *find = nArgc > 2 ? psArgs[2] : NULL;
		list = (listtype == TYPE_DSP ? DspSymbolsList : CpuSymbolsList);
		Symbols_Show(list, file, find);
		return DEBUGGER_CMDDONE;
	}

	/* free symbols? */
	if (strcmp(file, "free") == 0) {
		if (listtype == TYPE_DSP) {
			Symbols_Free(DspSymbolsList);
			DspSymbolsList = NULL;
		} else {
			Symbols_Free(CpuSymbolsList);
			CpuSymbolsList = NULL;
			if (CpuSymbolsChangedCallback)
				CpuSymbolsChangedCallback();
		}
		return DEBUGGER_CMDDONE;
	}

	/* get offsets */
	offsets[0] = 0;
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		if (i+2 < nArgc) {
			int dummy;
			Eval_Expression(psArgs[i+2], &(offsets[i]), &dummy, listtype==TYPE_DSP);
		} else {
			/* default to first (text) offset */
			offsets[i] = offsets[0];
		}
	}

	/* load symbols from GEMDOS HD program? */
	if (listtype == TYPE_CPU && strcmp(file, "prg") == 0) {
		file = CurrentProgramPath;
		if (!file) {
			fprintf(stderr, "ERROR: no program loaded (through GEMDOS HD emu)!\n");
			return DEBUGGER_CMDDONE;
		}
	}

	/* do actual loading */
	list = Symbols_Load(file, offsets, maxaddr, SYMTYPE_ALL);
	if (list) {
		if (listtype == TYPE_CPU) {
			Symbols_UpdateCpu(list, SYMBOLS_FOR_USER);
		} else {
			Symbols_UpdateDsp(list);
		}
	} else {
		DebugUI_PrintCmdHelp(psArgs[0]);
	}
	return DEBUGGER_CMDDONE;
}

int Symbols_CpuSymbolCount(void)
{
	if (!CpuSymbolsList)
		return 0;
	return CpuSymbolsList->namecount;
}

bool Symbols_GetCpuSymbol(int index, rdb_symbol_t* result)
{
	if (index >= Symbols_CpuSymbolCount())
		return false;

	const symbol_t* entry = CpuSymbolsList->names + index;
	result->name = entry->name;
	result->address = entry->address;
	result->type = symbol_char(entry->type);
	return true;
}

/* Function callback to inform when the symbol table has changed */
void Symbols_RegisterCpuChangedCallback(Symbols_ChangedCallback callback)
{
	CpuSymbolsChangedCallback = callback;
}

const char* Symbols_CpuGetCurrentPath(void)
{
	return CurrentProgramPath;
}