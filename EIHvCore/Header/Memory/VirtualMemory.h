#pragma once
#include <ntifs.h>

//Allocate All of Virtual Memory
class VirtualMemory
{
public:
	static BOOLEAN AllocateNonPagedPool(PVOID* Destination, SIZE_T Size, BOOLEAN Execution);
	static VOID FreeNonPagedPool(PVOID VirtualAddress);
	static ULONG_PTR VirtualAddressToPhysicalAddress(PVOID VirtualAddress);
};