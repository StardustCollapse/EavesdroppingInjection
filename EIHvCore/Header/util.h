#pragma once
#include <ntifs.h>

#define MAX_WAIT 64

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

	static VOID AcquireLock(volatile LONG* Lock);
	static VOID AcquireMonitorLock(volatile LONG* Lock);
	static VOID ReleaseLock(volatile LONG* Lock);

	static struct _SYSTEM_SERVICE_TABLE* GetSsdtTable();
	static PVOID GetSsdtFunctionAddress(ULONG SystemServiceNumber);
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