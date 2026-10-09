#include "Vcpu/Vcpu.h"
#include "Vmx/Vmx.h"
#include "Vmcs/Vmcs.h"
#include "Util.h"
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
		goto TurnOffVMX;
	}

	if (!Vmcs::VmxClearVmcs(CurrentVcpuState))
	{
		LogError("Failed to clear VMCS on processor %u", CurrentProcessorNumber);
		goto TurnOffVMX;
	}

	if (!Vmcs::VmxLoadVmcs(CurrentVcpuState))
	{
		LogError("Failed to load VMCS on processor %u", CurrentProcessorNumber);
		goto TurnOffVMX;
	}

	if (!Vmcs::VmxSetupVmcs(CurrentVcpuState, GuestStack))
	{
		LogError("Failed to setup VMCS on processor %u", CurrentProcessorNumber);
		goto TurnOffVMX;
	}

	CurrentVcpuState->VmcsLaunchStatus = TRUE;

	UCHAR Status = __vmx_vmlaunch();
	if (Status)
	{
		ULONG64 ErrorCode = 0;
		__vmx_vmread(Read_Only_Data_Fields::VM_Instruction_Error, &ErrorCode);
		LogError("VMLAUNCH failed on processor %u. Status: %u, Error code: 0x%llX",
			CurrentProcessorNumber, Status, ErrorCode);
	TurnOffVMX:
		LogInformation("Turning off VMX operation on processor %u", CurrentProcessorNumber);
		CurrentVcpuState->VmcsLaunchStatus = FALSE;
		if (CurrentVcpuState->VmxonStatus)
		{
			__vmx_off();
			CurrentVcpuState->VmxonStatus = FALSE;
		}
		if (__readcr4() & Util::Bit(13))
		{
			__writecr4(__readcr4() & ~Util::Bit(13));
		}
	}
	return;
}

EXTERN_C VOID
InitializeVcpuState(PVOID GuestStack)
{
	Vcpu::InitializeVcpuState(GuestStack);
	return;
}