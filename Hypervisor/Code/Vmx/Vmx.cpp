#include "Vmx/Vmx.h"
#include "Vcpu/Vcpu.h"
#include "Msr/Msr.h"
#include "Memory/VirtualMemory.h"
#include "Memory/PhysicalMemory.h"
#include "Log/Log.h"
#include "Register.h"

CPUID Vmx::cpuid = { 0 };
MSR_IA32_FEATURE_CONTROL Msr::IA32FeatureControl = { 0 };
MSR_IA32_VMX_BASIC Msr::IA32VmxBasic = { 0 };

BOOLEAN
Vmx::IsVmxSupported()
{
	__cpuid((PINT32)&Vmx::cpuid, 1);

	if (Vmx::cpuid.ecx.ECX_01H.VMX == FALSE)
	{
		LogError("VMX operation isn't supported");
		return FALSE;
	}

	Msr::IA32FeatureControl.MSR = __readmsr(MSRs::IA32_FEATURE_CONTROL);
	Msr::IA32VmxBasic.MSR = __readmsr(MSRs::IA32_VMX_BASIC);

	if(Msr::IA32FeatureControl.Fields.Lockbit == FALSE)
	{
		Msr::IA32FeatureControl.Fields.Lockbit = TRUE;
		Msr::IA32FeatureControl.Fields.EnableVmxonOutsideSMX = TRUE;
		__writemsr(MSRs::IA32_FEATURE_CONTROL, Msr::IA32FeatureControl.MSR);
	}
	else if (Msr::IA32FeatureControl.Fields.EnableVmxonOutsideSMX == FALSE)
	{
		LogError("VMX is locked");
		return FALSE;
	}

	if (
		Msr::IA32FeatureControl.Fields.EnableVmxonInSMX == TRUE ||
		Msr::IA32FeatureControl.Fields.EnableSenterLocalFunction == TRUE ||
		Msr::IA32FeatureControl.Fields.EnableSenterGlobal == TRUE
	)
	{
		LogWarning("SMX/TXT-related features are enabled."
				   "This hypervisor has not been validated in an SMX environment.");
	}
	return TRUE;
}

VOID
Vmx::PrepareVmxOperation()
{
	CR0 Cr0 = { 0 };
	CR4 Cr4 = { 0 };
	Cr0.All = __readcr0();
	Cr4.All = __readcr4();

	ULONG64 Cr0Fixed0 = __readmsr(MSRs::IA32_VMX_CR0_FIXED0);
	ULONG64 Cr0Fixed1 = __readmsr(MSRs::IA32_VMX_CR0_FIXED1);
	ULONG64 Cr4Fixed0 = __readmsr(MSRs::IA32_VMX_CR4_FIXED0);
	ULONG64 Cr4Fixed1 = __readmsr(MSRs::IA32_VMX_CR4_FIXED1);

	Cr0.All |= Cr0Fixed0;
	Cr0.All &= Cr0Fixed1;

	Cr4.All |= Cr4Fixed0;
	Cr4.All &= Cr4Fixed1;

	// Fixed bits guarantee valid CR0 state for VMX; no need to force PE/PG/NE.
	//Cr0.Flags.PE = 1;
	//Cr0.Flags.NE = 1;
	//Cr0.Flags.PG = 1;
	
	Cr4.Flags.VMXE = 1;

	__writecr0(Cr0.All);
	__writecr4(Cr4.All);
	return;
}

BOOLEAN
Vmx::VmxInitializeVmxonRegion(PVCPU_STATE CurrentCPU)
{
	CurrentCPU->VmxonAllocatedFailed = TRUE;

	CurrentCPU->VmxonRegionVirtualAddress = 
		(ULONG_PTR)PhysicalMemory::AllocateContiguousMemory(VMXON_SIZE);

	if (CurrentCPU->VmxonRegionVirtualAddress == NULL)
	{
		LogError("Failed to allocate VMXON region");
		return FALSE;
	}

	CurrentCPU->VmxonAllocatedFailed = FALSE;

	RtlZeroMemory((PVOID)CurrentCPU->VmxonRegionVirtualAddress, VMXON_SIZE);

	CurrentCPU->VmxonRegionPhysicalAddress =
		VirtualMemory::VirtualAddressToPhysicalAddress((PVOID)CurrentCPU->VmxonRegionVirtualAddress);

	*(PULONG64)CurrentCPU->VmxonRegionVirtualAddress = Msr::IA32VmxBasic.Fields.RevisionIdentifier;

	LogInformation("VMXON region allocated at virtual address: 0x%p, physical address: 0x%p",
		(PVOID)CurrentCPU->VmxonRegionVirtualAddress,
		(PVOID)CurrentCPU->VmxonRegionPhysicalAddress);

	return TRUE;
}

BOOLEAN
Vmx::VmxInitializeVmcsRegion(PVCPU_STATE CurrentCPU)
{
	CurrentCPU->VmcsAllocatedFailed = TRUE;

	CurrentCPU->VmcsRegionVirtualAddress =
		(ULONG_PTR)PhysicalMemory::AllocateContiguousMemory(VMCS_SIZE);

	if (CurrentCPU->VmcsRegionVirtualAddress == NULL)
	{
		LogError("Failed to allocate VMCS region");
		return FALSE;
	}

	CurrentCPU->VmcsAllocatedFailed = FALSE;

	RtlZeroMemory((PVOID)CurrentCPU->VmcsRegionVirtualAddress, VMCS_SIZE);

	CurrentCPU->VmcsRegionPhysicalAddress =
		VirtualMemory::VirtualAddressToPhysicalAddress((PVOID)CurrentCPU->VmcsRegionVirtualAddress);

	*(PULONG64)CurrentCPU->VmcsRegionVirtualAddress = Msr::IA32VmxBasic.Fields.RevisionIdentifier;

	LogInformation("VMCS region allocated at virtual address: 0x%p, physical address: 0x%p",
		(PVOID)CurrentCPU->VmcsRegionVirtualAddress,
		(PVOID)CurrentCPU->VmcsRegionPhysicalAddress);

	return TRUE;
}

BOOLEAN
Vmx::VmxInitializeMsrBitmap(PVCPU_STATE CurrentCPU)
{
	CurrentCPU->MsrBitmapAllocatedFailed = TRUE;

	CurrentCPU->MsrBitmapVirtualAddress =
		(ULONG_PTR)PhysicalMemory::AllocateContiguousMemory(MSR_BITMAP_SIZE);

	if (CurrentCPU->MsrBitmapVirtualAddress == NULL)
	{
		LogError("Failed to allocate MSR bitmap");
		return FALSE;
	}

	CurrentCPU->MsrBitmapAllocatedFailed = FALSE;

	RtlZeroMemory((PVOID)CurrentCPU->MsrBitmapVirtualAddress, MSR_BITMAP_SIZE);

	CurrentCPU->MsrBitmapPhysicalAddress =
		VirtualMemory::VirtualAddressToPhysicalAddress((PVOID)CurrentCPU->MsrBitmapVirtualAddress);

	LogInformation("MSR bitmap allocated at virtual address: 0x%p, physical address: 0x%p",
		(PVOID)CurrentCPU->MsrBitmapVirtualAddress,
		(PVOID)CurrentCPU->MsrBitmapPhysicalAddress);

	for (ULONG64 MsrStep = MSRs::IA32_VMX_BASIC; MsrStep <= MSRs::IA32_VMX_EXIT_CTLS2; MsrStep++)
	{
		if (!Msr::SetMsrBitmap(CurrentCPU->MsrBitmapVirtualAddress, MsrStep, TRUE, TRUE))
		{
			LogError("Invalid parameters sent to the SetMsrBitmap function");
			return FALSE;
		}
	}

	if (
		!(Msr::SetMsrBitmap(CurrentCPU->MsrBitmapVirtualAddress, MSRs::IA32_LSTAR, TRUE, TRUE) &&
		Msr::SetMsrBitmap(CurrentCPU->MsrBitmapVirtualAddress, MSRs::IA32_FEATURE_CONTROL, TRUE, FALSE))
	)
	{
		LogError("Invalid parameters sent to the SetMsrBitmap function");
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
Vmx::VmxInitializeVmmStack(PVCPU_STATE CurrentCPU)
{
	CurrentCPU->VmmStackAllocatedFailed = TRUE;

	BOOLEAN status = VirtualMemory::AllocateNonPagedPool((PVOID*)&CurrentCPU->VmmStack, VMM_STACK_SIZE, FALSE);

	if(status == FALSE)
	{
		LogError("Failed to allocate VMM stack");
		return FALSE;
	}
	CurrentCPU->VmmStackAllocatedFailed = FALSE;

	LogInformation("VMM stack allocated at virtual address: 0x%p", (PVOID)CurrentCPU->VmmStack);

	return TRUE;
}

BOOLEAN
Vmx::VmxEnterRootMode(PVCPU_STATE CurrentCPU)
{
	CurrentCPU->VmxonStatus = FALSE;
	UCHAR VmxonStatus = 3;

	VmxonStatus = __vmx_on(&CurrentCPU->VmxonRegionPhysicalAddress);
	if (VmxonStatus)
	{
		LogError("Failed to execute VMXON instruction. Error code: %d", VmxonStatus);
		return FALSE;
	}
	CurrentCPU->VmxonStatus = TRUE;

	LogInformation("VMXON executed successfully. VMXON region virtual address: 0x%p, physical address: 0x%p",
		(PVOID)CurrentCPU->VmxonRegionVirtualAddress,
		(PVOID)CurrentCPU->VmxonRegionPhysicalAddress);

	return TRUE;
}