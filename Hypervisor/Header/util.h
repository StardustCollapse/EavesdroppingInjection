#pragma once
#include "ntifs.h"

class Util
{
public:
	static ULONG64 HostCr3;

	static constexpr
	ULONG64 Bit(ULONG BitPosition)
	{
		return 1ULL << BitPosition;
	}

	static VOID SetBit(PUCHAR Value, ULONG64 BitPosition);
	static ULONG64 FindSystemDirectoryTableBase();
};


/* reference from HyperDbg */
typedef struct _NT_KPROCESS
	{
	DISPATCHER_HEADER Header;
	LIST_ENTRY ProfileListHead;
	ULONG_PTR DirectoryTableBase;
	UCHAR Data[1];
}NT_KPROCESS, * PNT_KPROCESS;
/* reference from HyperDbg */