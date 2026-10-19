#include "Memory/VirtualMemory.h"

#define Tag 0x45494856

BOOLEAN
VirtualMemory::AllocateNonPagedPool(
	PVOID* Destination,
	SIZE_T Size,
	BOOLEAN Execution
) {
	*Destination = ExAllocatePool2(
		Execution ? POOL_FLAG_NON_PAGED_EXECUTE : POOL_FLAG_NON_PAGED, Size, Tag);

	if (*Destination == NULL)
	{
		return FALSE;
	}
	
	RtlZeroMemory(*Destination, Size);

	return TRUE;
}

VOID
VirtualMemory::FreeNonPagedPool(PVOID VirtualAddress)
{
	if (VirtualAddress != NULL)
	{
		ExFreePoolWithTag(VirtualAddress, Tag);
	}
}

ULONG_PTR
VirtualMemory::VirtualAddressToPhysicalAddress(PVOID VirtualAddress)
{
	return MmGetPhysicalAddress(VirtualAddress).QuadPart;
}