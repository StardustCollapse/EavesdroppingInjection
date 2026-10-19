#pragma once
#include <ntifs.h>
#include "VcpuState.h"

//Manage Per-CPU
class Vcpu
{
public:
	static PVCPU_STATE VcpuState;
	static VOID InitializeVcpuState(PVOID GuestStack);
};

EXTERN_C VOID InitializeVcpuState(PVOID GuestStack);