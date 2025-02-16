#include "stgen.h"

namespace stgen
{
	const char* GetString(const StringDef* strings, uint32_t value)
	{
		while (strings->string)
		{
			if (value == strings->value)
				return strings->string;
			++strings;
		}
		return "?";
	}
}
