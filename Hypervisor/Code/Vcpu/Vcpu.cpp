#include "Vcpu/Vcpu.h"
#include "Vmx/Vmx.h"
#include "Vmcs/Vmcs.h"
#include "Log/Log.h"

PVCPU_STATE Vcpu::VcpuState = NULL;

VOID
Vcpu::InitializeVcpuState(PVOID GuestStack)
{
	ULONG CurrentProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
	PVCPU_STATE CurrentVcpuState = &Vcpu::VcpuState[CurrentProcessorNumber];

	Vmx::PrepareVmxOperation();

	if (!Vmx::VmxEnterRootMode(CurrentVcpuState))
	{
		LogError("Failed to enter VMX root mode on processor %u", CurrentProcessorNumber);
		return;
	}

	if (!Vmcs::VmxClearVmcs(CurrentVcpuState))
	{
		LogError("Failed to clear VMCS on processor %u", CurrentProcessorNumber);
		return;
	}

	if (!Vmcs::VmxLoadVmcs(CurrentVcpuState))
	{
		LogError("Failed to load VMCS on processor %u", CurrentProcessorNumber);
		return;
	}

	Vmcs::VmxSetupVmcs(CurrentVcpuState, GuestStack);

	CurrentVcpuState->VmcsLaunchStatus = TRUE;

	UCHAR Status = __vmx_vmlaunch();
	if (Status)
	{
		ULONG64 ErrorCode = 0;
		__vmx_vmread(Read_Only_Data_Fields::VM_Instruction_Error, &ErrorCode);
		LogError("VMLAUNCH failed on processor %u. Status: %u, Error code: 0x%llX",
			CurrentProcessorNumber, Status, ErrorCode);
		CurrentVcpuState->VmcsLaunchStatus = FALSE;
		__vmx_off();
	}
	return;
}

EXTERN_C VOID
InitializeVcpuState(PVOID GuestStack)
{
	Vcpu::InitializeVcpuState(GuestStack);
	return;
}