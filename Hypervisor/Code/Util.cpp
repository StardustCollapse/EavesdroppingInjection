#include "Util.h"

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
ULONG64 Util::FindSystemDirectoryTableBase()
{
	// Return CR3 of the system process.
	NT_KPROCESS* SystemProcess = (NT_KPROCESS*)(PsInitialSystemProcess);
	return SystemProcess->DirectoryTableBase;
}
/* reference from HyperDbg */