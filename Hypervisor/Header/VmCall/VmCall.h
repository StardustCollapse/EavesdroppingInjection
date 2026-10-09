#pragma once
#include <ntifs.h>
#include "VmExit/VmExit.h"

#define VMCALL_STOP_HV 0x53544F505F4856ULL //STOP_HV

EXTERN_C NTSTATUS AsmDisableVmxOperation(ULONG64 Cr0, ULONG64 Cr3, ULONG64 Cr4, PGUEST_CONTEXT Context);

//Manage VmCall Handler
class VmCall
{
public:
	static VOID VmcallStopHv(PGUEST_CONTEXT Context);
};