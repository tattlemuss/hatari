#ifndef STGEN_DEF_H
#define STGEN_DEF_H
#include <stdint.h>

namespace stgen
{
	struct StringDef
	{
		uint32_t value;
		const char* string;
	};

	struct FieldDef
	{
		uint32_t regAddr;
		uint32_t mask;
		uint8_t size;
		uint8_t shift;
		const char* name;
		const StringDef* strings;
		const char* comment;
	};

	extern const char* GetString(const StringDef* strings, uint32_t value);
}

#endif // STGEN_DEF_H
