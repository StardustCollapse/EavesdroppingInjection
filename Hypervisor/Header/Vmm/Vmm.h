#pragma once
#include <ntifs.h>
#include "VmmState.h"

//Manage All CPU AND VT CONDITION
class Vmm
{
public:
	static VMM_STATE VmmState;

	static NTSTATUS LoadVMM();	//return STATUS_SUCCESS / UNSUCCESSFUL
	static NTSTATUS UnLoadVMM();	//return STATUS_SUCCESS / UNSUCCESSFUL, need to check Hypervisor is running or don't UnLoad
};