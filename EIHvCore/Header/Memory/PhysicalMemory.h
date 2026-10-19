#pragma once
#include <ntifs.h>

//Allocate All of Physical Memory
class PhysicalMemory
{
public:
	static PVOID AllocateContiguousMemory(SIZE_T Size);
	static VOID FreeContiguousMemory(PVOID VirtualAddress);
};