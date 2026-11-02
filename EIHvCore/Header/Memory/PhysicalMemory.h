#pragma once
#include <ntifs.h>

//Allocate All of Physical Memory
class PhysicalMemory
{
public:
	static BOOLEAN AllocateContiguousMemory(PVOID* Destination,	SIZE_T Size);
	static VOID FreeContiguousMemory(PVOID VirtualAddress);
};