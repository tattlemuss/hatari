#ifndef HOPPER68_TIMING_H
#define HOPPER68_TIMING_H
#include <cstdint>
#include <vector>

namespace hop68
{
struct instruction;

#define PAIR_FRONT	(1<<0)		// If true, this instruction can pair with the previous one
#define PAIR_BACK	(1<<1)		// If true, this instruction can pair with the next one

struct timing
{
	uint16_t min;
	uint16_t max;
	uint8_t	flags;
};

extern int calc_timing(const instruction& inst, timing& result);
}
#endif
