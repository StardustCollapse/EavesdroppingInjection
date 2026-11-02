#include "Memory/PhysicalMemory.h"

BOOLEAN 
PhysicalMemory::AllocateContiguousMemory(PVOID* Destination, SIZE_T Size)
{
	PHYSICAL_ADDRESS PhysicalAddressMax = { 0 };
	PhysicalAddressMax.QuadPart = MAXULONG64;
	*Destination = MmAllocateContiguousMemory(Size, PhysicalAddressMax);

	if (*Destination == NULL)
	{
		return FALSE;
	}

	RtlZeroMemory(*Destination, Size);

	return TRUE;
}

VOID 
PhysicalMemory::FreeContiguousMemory(PVOID VirtualAddress)
{
	if (VirtualAddress != NULL)
	{
		MmFreeContiguousMemory(VirtualAddress);
	}
}