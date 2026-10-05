#pragma once
#include <ntifs.h>
#include "VmmState.h"

//Manage All CPU AND VT CONDITION
class Vmm
{
public:
	static VMM_STATE VmmState;

	static NTSTATUS LoadVMM();
	static NTSTATUS UnLoadVMM();
	static VOID FreeVmmMemory();
	static ULONG_PTR DisableVmxOperation(ULONG_PTR Argument);
};