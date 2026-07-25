#pragma once
#include "ntifs.h"

class Util
{
public:
	static constexpr
	ULONG64 Bit(ULONG BitPosition)
	{
		return 1ULL << BitPosition;
	}

	static constexpr
	ULONG64 SetBit(ULONG64 Value, ULONG BitPosition)
	{
		return Value | (1ULL << BitPosition);
	}
};