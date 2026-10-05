#include "Vmm/Vmm.h"
#include "Vmx/Vmx.h"
#include "Vcpu/Vcpu.h"
#include "Memory/VirtualMemory.h"
#include "Memory/PhysicalMemory.h"
#include "VmExit/VmExit.h"
#include "Util.h"
#include "Log/Log.h"

VMM_STATE Vmm::VmmState = { 0 };

NTSTATUS
Vmm::LoadVMM()
{
	PAGED_CODE();

	if (Vmm::VmmState.VmmFlags.Bit.VmmStarted == TRUE)
	{
		LogWarning("Hypervisor is already running");
		return STATUS_SUCCESS;
	}

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
			Vmm::FreeVmmMemory();
			return STATUS_UNSUCCESSFUL;
		}
	}

	LogInformation("Initialize Vcpu states successfully");

	Util::HostCr3 = Util::FindSystemDirectoryTableBase();

	//save Guest Stack and call Vcpu::InitializeVcpuState(PVOID GuestStack)
	KeIpiGenericCall(AsmVmxContextSaveState, NULL);

	// Verify that all cores launched successfully
	BOOLEAN AllLaunched = TRUE;
	for (ULONG CurrentProcessorNumber = 0;
		 CurrentProcessorNumber < Vmm::VmmState.LogicalProcessorsCount;
		 CurrentProcessorNumber++)
	{
		if (!Vcpu::VcpuState[CurrentProcessorNumber].VmcsLaunchStatus)
		{
			//here can set affinity to the failed processor and log error
			AllLaunched = FALSE;
			LogError("Processor %u failed to launch VMCS", CurrentProcessorNumber);
			break;
		}
	}

	if (!AllLaunched)
	{
		LogError("Failed to Initialize all processors");
		Vmm::UnLoadVMM();
		return STATUS_UNSUCCESSFUL;
	}

	Vmm::VmmState.VmmFlags.Bit.VmmStarted = TRUE;
	LogInformation("Hypervisor loaded and started successfully");

	return STATUS_SUCCESS;
}

NTSTATUS
Vmm::UnLoadVMM()
{
	if (Vmm::VmmState.VmmFlags.Bit.VmmStarted == FALSE)
	{
		LogWarning("Hypervisor is not running, skipping unload");
		Vmm::FreeVmmMemory();
		return STATUS_SUCCESS;
	}

	LogInformation("Stopping Hypervisor on all logical processors...");

	// Disable VMX operation on all processors
	KeIpiGenericCall(Vmm::DisableVmxOperation, 0);

	// Free all allocated memory
	Vmm::FreeVmmMemory();

	Vmm::VmmState.VmmFlags.Bit.VmmStarted = FALSE;

	LogInformation("Hypervisor stopped and unloaded successfully");
	return STATUS_SUCCESS;
}

VOID
Vmm::FreeVmmMemory()
{
	if (Vcpu::VcpuState == NULL)
	{
		return;
	}

	for (ULONG CurrentProcessorNumber = 0;
		CurrentProcessorNumber < Vmm::VmmState.LogicalProcessorsCount;
		CurrentProcessorNumber++)
	{
		PVCPU_STATE CurrentVcpuState = &Vcpu::VcpuState[CurrentProcessorNumber];

		if (CurrentVcpuState->VmxonRegionVirtualAddress)
		{
			PhysicalMemory::FreeContiguousMemory((PVOID)CurrentVcpuState->VmxonRegionVirtualAddress);
			CurrentVcpuState->VmxonRegionVirtualAddress = NULL;
			CurrentVcpuState->VmxonRegionPhysicalAddress = NULL;
		}

		if (CurrentVcpuState->VmcsRegionVirtualAddress)
		{
			PhysicalMemory::FreeContiguousMemory((PVOID)CurrentVcpuState->VmcsRegionVirtualAddress);
			CurrentVcpuState->VmcsRegionVirtualAddress = NULL;
			CurrentVcpuState->VmcsRegionPhysicalAddress = NULL;
		}

		if (CurrentVcpuState->MsrBitmapVirtualAddress)
		{
			PhysicalMemory::FreeContiguousMemory((PVOID)CurrentVcpuState->MsrBitmapVirtualAddress);
			CurrentVcpuState->MsrBitmapVirtualAddress = NULL;
			CurrentVcpuState->MsrBitmapPhysicalAddress = NULL;
		}

		if (CurrentVcpuState->VmmStack)
		{
			VirtualMemory::FreeNonPagedPool((PVOID)CurrentVcpuState->VmmStack);
			CurrentVcpuState->VmmStack = NULL;
		}
	}

	VirtualMemory::FreeNonPagedPool((PVOID)Vcpu::VcpuState);
	Vcpu::VcpuState = NULL;

	LogInformation("All VMM memory is freed successfully");
}

ULONG_PTR
Vmm::DisableVmxOperation(ULONG_PTR Argument)
{
	UNREFERENCED_PARAMETER(Argument);
	ULONG CurrentProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);

	if (Vcpu::VcpuState && Vcpu::VcpuState[CurrentProcessorNumber].VmcsLaunchStatus)
	{
		AsmDisableVmxOperationVmcall();
	}
	if (Vcpu::VcpuState && Vcpu::VcpuState[CurrentProcessorNumber].VmxonStatus)
	{
		__vmx_off();
		Vcpu::VcpuState[CurrentProcessorNumber].VmxonStatus = FALSE;
	}
	if (__readcr4() & Util::Bit(13))
	{
		__writecr4(__readcr4() & ~Util::Bit(13));
	}

	return 0;
}