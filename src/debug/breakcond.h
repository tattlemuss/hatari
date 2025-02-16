/*
  Hatari - breakcond.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_BREAKCOND_H
#define HATARI_BREAKCOND_H

/* for debugui.c */
extern bool BreakCond_Save(const char *filename);

/* for debugcpu.c & debugdsp.c */
extern const char BreakCond_Description[];
extern const char BreakAddr_Description[];

extern bool BreakCond_MatchCpu(void);
extern bool BreakCond_MatchDsp(void);
extern int BreakCond_CpuBreakPointCount(void);
extern int BreakCond_DspBreakPointCount(void);
extern bool BreakCond_Command(const char *expression, bool bForDsp);
extern bool BreakAddr_Command(char *expression, bool bforDsp);

/* extra functions exported for the test code */
extern int BreakCond_MatchCpuExpression(int position, const char *expression);

/* Remote Debugging Functions */
typedef struct {
	const char *expression;
	int ccount;	/* condition count */
	int hits;	/* how many times breakpoint hit */
	bool once;	/* one-time only */
	bool quiet;	/* do not report, only count */
	bool trace;	/* write to log */
} bc_breakpoint_query_t;

/* Remote debugging: query data for CPU breakpoint N.
	Breakpoints are indexed from 1!
	Returns true if data found */
extern bool BreakCond_GetCpuBreakpointInfo(int position, bc_breakpoint_query_t *result);
/* Remote debugging: query data for DSP breakpoint N.
	Breakpoints are indexed from 1!
	Returns true if data found */
extern bool BreakCond_GetDspBreakpointInfo(int position, bc_breakpoint_query_t *result);

extern bool BreakCond_RemoveCpuBreakpoint(int position);
extern bool BreakCond_RemoveDspBreakpoint(int position);

#endif
