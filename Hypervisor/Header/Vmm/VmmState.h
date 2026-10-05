#pragma once
#include <ntifs.h>

typedef struct __VMM_STATE
{
	ULONG64 LogicalProcessorsCount;
	union __VMM_FLAGS
	{
		struct __VMM_FLAGS_BIT
		{
			ULONG64 VmxSupported : 1;
			ULONG64 VmmStarted : 1;
			ULONG64 Reversed : 62;
		}Bit;
		ULONG64 Flags;
	}VmmFlags;
}VMM_STATE, * PVMM_STATE;