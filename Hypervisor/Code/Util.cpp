#include "Util.h"
#include "Ssdt/Ssdt.h"
#include <intrin.h>

ULONG64 Util::HostCr3 = 0;

VOID
Util::SetBit(PUCHAR Value, ULONG64 BitPosition)
{
	ULONG64 ByteOffset = BitPosition / 8;
	ULONG64 BitPositionInByte = BitPosition % 8;

	Value[ByteOffset] |= (1 << BitPositionInByte);
	return;
}


/* reference from HyperDbg */
/* Find cr3 of system process*/
ULONG64
Util::FindSystemDirectoryTableBase()
{
	// Return CR3 of the system process.
	NT_KPROCESS* SystemProcess = (NT_KPROCESS*)(PsInitialSystemProcess);
	return SystemProcess->DirectoryTableBase;
}
/* reference from HyperDbg */

VOID
Util::AcquireLock(volatile LONG* Lock)
{
	ULONG Wait = 1;

	while (_InterlockedCompareExchange(Lock, 1, 0) != 0)
	{
		while (*Lock != 0)
		{
			for (ULONG i = 0; i < Wait; i++) _mm_pause();

			if (Wait < MAX_WAIT) Wait <<= 1;
		}
		Wait = 1;
	}
}

VOID
Util::AcquireMonitorLock(volatile LONG* Lock)
{
	while (_InterlockedCompareExchange(Lock, 1, 0) != 0)
	{
		while (*Lock != 0)
		{
			_mm_monitor((void const *)Lock, 0, 0);
			if (*Lock != 0)
			{
				//allow interupt to awake core
				_mm_mwait(1, 0);
			}
		}
	}
}

VOID
Util::ReleaseLock(volatile LONG* Lock)
{
	_InterlockedExchange(Lock, 0);
}

struct _SYSTEM_SERVICE_TABLE*
Util::GetSsdtTable()
{
	return Ssdt::GetTable();
}

PVOID
Util::GetSsdtFunctionAddress(ULONG SystemServiceNumber)
{
	return Ssdt::GetFunctionAddress(SystemServiceNumber);
}