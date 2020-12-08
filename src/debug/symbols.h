/*
 * Hatari - symbols.h
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_SYMBOLS_H
#define HATARI_SYMBOLS_H

typedef enum {
	SYMTYPE_TEXT = 1,  /* Needs to be smallest number for sorting! */
	SYMTYPE_DATA = 2,
	SYMTYPE_BSS  = 4,
	SYMTYPE_ABS  = 8,
	SYMTYPE_ALL  = SYMTYPE_TEXT|SYMTYPE_DATA|SYMTYPE_BSS|SYMTYPE_ABS
} symtype_t;

extern const char Symbols_Description[];

/* readline completion support functions for CPU */
extern char* Symbols_MatchCpuAddress(const char *text, int state);
extern char* Symbols_MatchCpuCodeAddress(const char *text, int state);
extern char* Symbols_MatchCpuDataAddress(const char *text, int state);
/* readline completion support functions for DSP */
extern char* Symbols_MatchDspAddress(const char *text, int state);
extern char* Symbols_MatchDspCodeAddress(const char *text, int state);
extern char* Symbols_MatchDspDataAddress(const char *text, int state);
/* symbol name -> address search */
extern bool Symbols_GetCpuAddress(symtype_t symtype, const char *name, Uint32 *addr);
extern bool Symbols_GetDspAddress(symtype_t symtype, const char *name, Uint32 *addr);
/* symbol address -> name search */
extern const char* Symbols_GetByCpuAddress(Uint32 addr, symtype_t symtype);
extern const char* Symbols_GetByDspAddress(Uint32 addr, symtype_t symtype);
extern const char* Symbols_GetBeforeCpuAddress(Uint32 *addr);
extern const char* Symbols_GetBeforeDspAddress(Uint32 *addr);
/* TEXT symbol address -> index */
extern int Symbols_GetCpuCodeIndex(Uint32 addr);
extern int Symbols_GetDspCodeIndex(Uint32 addr);
/* how many TEXT symbols are loaded */
extern int Symbols_CpuCodeCount(void);
extern int Symbols_DspCodeCount(void);
/* handlers for automatic program symbol loading */
extern void Symbols_RemoveCurrentProgram(void);
extern void Symbols_ChangeCurrentProgram(const char *path);
extern void Symbols_ShowCurrentProgramPath(FILE *fp);
extern void Symbols_LoadCurrentProgram(void);
/* symbols/dspsymbols command parsing */
extern char *Symbols_MatchCommand(const char *text, int state);
extern int Symbols_Command(int nArgc, char *psArgs[]);

/* Remote debug code */
typedef struct {
	char *name;
	Uint32 address;
	char type;
} rdb_symbol_t;
extern int Symbols_CpuSymbolCount(void);
extern bool Symbols_GetCpuSymbol(int index, rdb_symbol_t* result);

#endif
