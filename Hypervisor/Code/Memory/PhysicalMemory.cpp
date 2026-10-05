#include "Memory/PhysicalMemory.h"

PVOID 
PhysicalMemory::AllocateContiguousMemory(SIZE_T Size)
{
	PHYSICAL_ADDRESS PhysicalAddressMax = { 0 };
	PhysicalAddressMax.QuadPart = MAXULONG64;
	return MmAllocateContiguousMemory(Size, PhysicalAddressMax);
}

VOID 
PhysicalMemory::FreeContiguousMemory(PVOID VirtualAddress)
{
	MmFreeContiguousMemory(VirtualAddress);
}