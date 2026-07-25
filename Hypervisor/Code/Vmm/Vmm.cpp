#include "Vmm/Vmm.h"
#include "Vmx/Vmx.h"
#include "Vcpu/Vcpu.h"
#include "Memory/VirtualMemory.h"
#include "Log/Log.h"

VMM_STATE Vmm::VmmState = { 0 };

NTSTATUS
Vmm::LoadVMM()
{
	PAGED_CODE();

	Vmm::VmmState.LogicalProcessorsCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

	if (Vmx::IsVmxSupported() == FALSE)
	{
		LogError("VMX is not supported");
		return STATUS_UNSUCCESSFUL;
	}

	LogInformation("VMX is supported. Logical processors count: %llu", Vmm::VmmState.LogicalProcessorsCount);

	BOOLEAN status = VirtualMemory::AllocateNonPagedPool(
		(PVOID*)&Vcpu::VcpuState, sizeof(VCPU_STATE) * Vmm::VmmState.LogicalProcessorsCount, FALSE);

	if (status == FALSE)
	{
		LogError("Failed to allocate Vcpu memory");
		return STATUS_UNSUCCESSFUL;
	}

	LogInformation("Vcpu memory allocated at virtual address: 0x%p", (PVOID)Vcpu::VcpuState);

	for 
	(
		ULONG CurrentProcessorNumber = 0;
		CurrentProcessorNumber < Vmm::VmmState.LogicalProcessorsCount;
		CurrentProcessorNumber++
	){
		PVCPU_STATE CurrentVcpuState = &Vcpu::VcpuState[CurrentProcessorNumber];

		Vmx::VmxInitializeVmxonRegion(CurrentVcpuState);
		Vmx::VmxInitializeVmcsRegion(CurrentVcpuState);
		Vmx::VmxInitializeMsrBitmap(CurrentVcpuState);
		Vmx::VmxInitializeVmmStack(CurrentVcpuState);

		BOOLEAN InitializeStatusFailed = 
			CurrentVcpuState->VmmStackAllocatedFailed |
			CurrentVcpuState->MsrBitmapAllocatedFailed |
			CurrentVcpuState->VmcsAllocatedFailed |
			CurrentVcpuState->VmxonAllocatedFailed;

		if (InitializeStatusFailed == TRUE)
		{
			LogError("Initialize Vcpu states failed");
			return STATUS_UNSUCCESSFUL;
		}
	}

	LogInformation("Initialize Vcpu states successfully");

	//save Guest Stack and call Vcpu::InitializeVcpuState(PVOID GuestStack)
	KeIpiGenericCall(AsmVmxContextSaveState, NULL);
	
	return STATUS_SUCCESS;
}

NTSTATUS
Vmm::UnLoadVMM()
{
	//InComplete

	return STATUS_SUCCESS;
}