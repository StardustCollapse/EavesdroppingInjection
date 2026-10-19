#include "Ept/Ept.h"
#include "Memory/PhysicalMemory.h"
#include "Memory/VirtualMemory.h"
#include "Msr/MsrControlField.h"
#include "Util.h"
#include "Log/Log.h"
#include "Vcpu/Vcpu.h"
#include "MSR/Msr.h"
#include "EptHook/EptHook.h"

EPT_STATE Ept::EptState = { 0 };

BOOLEAN
Ept::IsEptSupported()
{
	BOOLEAN UseTrueMsrs = (Msr::IA32VmxBasic.Fields.VmxCapabilityHint != 0);
	ULONG ProcCtlsMsr = UseTrueMsrs ? MSRs::IA32_VMX_TRUE_PROCBASED_CTLS : MSRs::IA32_VMX_PROCBASED_CTLS;
	MSR_INFORMATION ProcCtlsInfo;
	ProcCtlsInfo.QuadPart = __readmsr(ProcCtlsMsr);

	if (!(ProcCtlsInfo.HighPart & VM_Execution_Controls::PRIMARY_PROCESSOR_BASED_ACTIVATE_SECONDARY_CONTROLS))
	{
		LogError("CPU is not supported Secondary Processor Controls");
		return FALSE;
	}

	MSR_INFORMATION SecondaryProcCtlsMsr;
	SecondaryProcCtlsMsr.QuadPart = __readmsr(MSRs::IA32_VMX_PROCBASED_CTLS2);
	if (!(SecondaryProcCtlsMsr.HighPart & VM_Execution_Controls::SECONDARY_PROCESSOR_BASED_ENABLE_EPT))
	{
		LogError("EPT is not supported by CPU Secondary Processor Controls");
		return FALSE;
	}

	if (!(SecondaryProcCtlsMsr.HighPart & VM_Execution_Controls::SECONDARY_PROCESSOR_BASED_ENABLE_VPID))
	{
		LogError("VPID is not supported by CPU Secondary Processor Controls");
		return FALSE;
	}

	// Check EPT Capabilities
	MSR_INFORMATION EptVpidCap;
	EptVpidCap.QuadPart = __readmsr(MSRs::IA32_VMX_EPT_VPID_CAP);

	// Page-walk length of 4 (4-level paging: PML4 -> PDPT -> PD -> PT)
	if (!(EptVpidCap.QuadPart & Util::Bit(6)))
	{
		LogError("EPT page-walk length of 4 is not supported");
		return FALSE;
	}

	// 2MB large pages support
	if (!(EptVpidCap.QuadPart & Util::Bit(16)))
	{
		LogError("EPT 2MB large pages are not supported");
		return FALSE;
	}

	// INVEPT instruction support
	if (!(EptVpidCap.QuadPart & Util::Bit(20)))
	{
		LogWarning("INVEPT instruction is not supported");
		return FALSE;
	}

	// INVEPT single-context instruction support
	if (!(EptVpidCap.QuadPart & Util::Bit(25)))
	{
		LogWarning("INVEPT single-context instruction is not supported");
		return FALSE;
	}

	// INVEPT all-context instruction support
	if (!(EptVpidCap.QuadPart & Util::Bit(26)))
	{
		LogWarning("INVEPT all-context instruction is not supported");
		return FALSE;
	}

	LogInformation("EPT capabilities verified successfully. IA32_VMX_EPT_VPID_CAP: 0x%llx", EptVpidCap.QuadPart);
	return TRUE;
}

BOOLEAN
Ept::InitializeEpt()
{
	if (!Ept::IsEptSupported())
	{
		return FALSE;
	}

	Ept::EptState.Pml4Table = (PEPT_PML4E)PhysicalMemory::AllocateContiguousMemory(PAGE_SIZE);
	if (Ept::EptState.Pml4Table == NULL)
	{
		LogError("Failed to allocate EPT PML4 table");
		Ept::DestroyEpt();
		return FALSE;
	}
	RtlZeroMemory(Ept::EptState.Pml4Table, PAGE_SIZE);
	Ept::EptState.Pml4PhysicalAddress = VirtualMemory::VirtualAddressToPhysicalAddress(Ept::EptState.Pml4Table);

	Ept::EptState.PdptTable = (PEPT_PDPTE)PhysicalMemory::AllocateContiguousMemory(PAGE_SIZE * EPT_PML4E_COUNT);
	if (Ept::EptState.PdptTable == NULL)
	{
		LogError("Failed to allocate EPT PDPT table");
		Ept::DestroyEpt();
		return FALSE;
	}
	RtlZeroMemory(Ept::EptState.PdptTable, PAGE_SIZE * EPT_PML4E_COUNT);
	Ept::EptState.PdptPhysicalAddress = VirtualMemory::VirtualAddressToPhysicalAddress(Ept::EptState.PdptTable);

	Ept::EptState.PdTables = (PEPT_PAGE_DIRECTORY)PhysicalMemory::AllocateContiguousMemory(PAGE_SIZE * EPT_PML3E_COUNT * EPT_PML4E_COUNT);
	if (Ept::EptState.PdTables == NULL)
	{
		LogError("Failed to allocate EPT Page Directory tables (2MB)");
		Ept::DestroyEpt();
		return FALSE;
	}
	RtlZeroMemory(Ept::EptState.PdTables, PAGE_SIZE * EPT_PML3E_COUNT * EPT_PML4E_COUNT);
	Ept::EptState.PdTablesPhysicalAddress = VirtualMemory::VirtualAddressToPhysicalAddress(Ept::EptState.PdTables);

	//can use __stosq() function

	for (ULONG i = 0; i < EPT_PML4E_COUNT; i++)
	{
		ULONG64 PdptPhysicalAddress = Ept::EptState.PdptPhysicalAddress + (i * PAGE_SIZE);

		Ept::EptState.Pml4Table[i].Fields.Read = 1;
		Ept::EptState.Pml4Table[i].Fields.Write = 1;
		Ept::EptState.Pml4Table[i].Fields.Execute = 1;
		Ept::EptState.Pml4Table[i].Fields.PdptePhysicalAddress = PdptPhysicalAddress >> 12;
	}

	for (ULONG i = 0; i < EPT_PML4E_COUNT; i++)
	{
		for (ULONG j = 0; j < EPT_PML3E_COUNT; j++)
		{
			ULONG64 PdPhysicalAddress = Ept::EptState.PdTablesPhysicalAddress + (((ULONG64)i * EPT_PML3E_COUNT + j) * PAGE_SIZE);
			PEPT_PDPTE Pdpte = &Ept::EptState.PdptTable[(ULONG64)i * EPT_PML3E_COUNT + j];

			Pdpte->Fields.Read = 1;
			Pdpte->Fields.Write = 1;
			Pdpte->Fields.Execute = 1;
			Pdpte->Fields.LargePage = 0; // References a Page Directory
			Pdpte->Fields.PdePhysicalAddress = PdPhysicalAddress >> 12;
		}
	}

	for (ULONG i = 0; i < EPT_PML4E_COUNT; i++)
	{
		for (ULONG j = 0; j < EPT_PML3E_COUNT; j++)
		{
			for (ULONG k = 0; k < EPT_PML2E_COUNT; k++)
			{
				ULONG64 PhysicalAddress = ((ULONG64)i << 39) | ((ULONG64)j << 30) | ((ULONG64)k << 21);
				PEPT_PDE_2MB Pde = &Ept::EptState.PdTables[(ULONG64)i * EPT_PML3E_COUNT + j].Entries[k];

				Pde->Fields.Read = 1;
				Pde->Fields.Write = 1;
				Pde->Fields.Execute = 1;
				Pde->Fields.MemoryType = 6;      // Write-Back (WB)
				Pde->Fields.IgnorePat = 0;       // Combine with guest MTRR
				Pde->Fields.LargePage = 1;       // 2MB Large Page
				Pde->Fields.PageFrameNumber = PhysicalAddress >> 21;
			}
		}
	}

	MSR_INFORMATION EptVpidCap;
	EptVpidCap.QuadPart = __readmsr(MSRs::IA32_VMX_EPT_VPID_CAP);
	ULONG EptMemoryType = (EptVpidCap.HighPart & Util::Bit(14)) ? 6 : 0; // WB (6) if supported, else UC (0)

	Ept::EptState.EptPointer.Value = 0;
	Ept::EptState.EptPointer.Fields.MemoryType = EptMemoryType;
	Ept::EptState.EptPointer.Fields.PageWalkLength = 3; // 4 levels (0 - 3)
	Ept::EptState.EptPointer.Fields.EnableAccessedDirtyFlags = 0;
	Ept::EptState.EptPointer.Fields.Pml4PhysicalAddress = Ept::EptState.Pml4PhysicalAddress >> 12;

	Ept::EptState.SplitPageListHead = NULL;

	LogInformation("EPT Identity Mapping initialized (%llx). EPTP: 0x%llx", EPT_MAPPED_SIZE, Ept::EptState.EptPointer.Value);
	return TRUE;
}

VOID
Ept::DestroyEpt()
{
	PSPLIT_PAGE_NODE CurrentNode = Ept::EptState.SplitPageListHead;
	while (CurrentNode != NULL)
	{
		PSPLIT_PAGE_NODE NextNode = CurrentNode->Next;
		if (CurrentNode->PageTableVirtualAddress != NULL)
		{
			PhysicalMemory::FreeContiguousMemory(CurrentNode->PageTableVirtualAddress);
		}
		VirtualMemory::FreeNonPagedPool(CurrentNode);
		CurrentNode = NextNode;
	}
	Ept::EptState.SplitPageListHead = NULL;

	if (Ept::EptState.PdTables != NULL)
	{
		PhysicalMemory::FreeContiguousMemory(Ept::EptState.PdTables);
		Ept::EptState.PdTables = NULL;
		Ept::EptState.PdTablesPhysicalAddress = 0;
	}

	if (Ept::EptState.PdptTable != NULL)
	{
		PhysicalMemory::FreeContiguousMemory(Ept::EptState.PdptTable);
		Ept::EptState.PdptTable = NULL;
		Ept::EptState.PdptPhysicalAddress = 0;
	}

	if (Ept::EptState.Pml4Table != NULL)
	{
		PhysicalMemory::FreeContiguousMemory(Ept::EptState.Pml4Table);
		Ept::EptState.Pml4Table = NULL;
		Ept::EptState.Pml4PhysicalAddress = 0;
	}

	Ept::EptState.EptPointer.Value = 0;
	LogInformation("EPT structures destroyed and memory freed successfully");
}

EPTP
Ept::GetEptPointer()
{
	return Ept::EptState.EptPointer;
}

ULONG_PTR
Ept::InveptAllContexts(ULONG_PTR Arguments)
{
	UNREFERENCED_PARAMETER(Arguments);
	ULONG CurrentProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
	if (Vcpu::VcpuState && Vcpu::VcpuState[CurrentProcessorNumber].VmxonStatus)
	{
		INVEPT_DESCRIPTOR Descriptor = { 0 };
		return (ULONG_PTR)AsmInvept(INVEPT_ALL_CONTEXTS, &Descriptor);
	}
	return 0;
}

ULONG_PTR
Ept::InveptSingleContext(ULONG64 EptPointer)
{
	ULONG CurrentProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
	if (Vcpu::VcpuState && Vcpu::VcpuState[CurrentProcessorNumber].VmxonStatus)
	{
		INVEPT_DESCRIPTOR Descriptor = { 0 };
		Descriptor.EptPointer = EptPointer;
		return (ULONG_PTR)AsmInvept(INVEPT_SINGLE_CONTEXT, &Descriptor);
	}
	return 0;
}

PEPT_PTE
Ept::SplitLargePageTo4Kb(ULONG64 PhysicalAddress)
{
	if (PhysicalAddress >= EPT_MAPPED_SIZE || Ept::EptState.PdTables == NULL)
	{
		LogError("PhysicalAddress 0x%llx exceeds mapped 0x%llx range", PhysicalAddress, EPT_MAPPED_SIZE);
		return NULL;
	}

	ULONG64 Pml4Index = EPT_PML4_INDEX(PhysicalAddress);
	ULONG64 PdptIndex = EPT_PML3_INDEX(PhysicalAddress);
	ULONG64 PdIndex = EPT_PML2_INDEX(PhysicalAddress);

	if (Pml4Index >= EPT_PML4E_COUNT || PdptIndex >= EPT_PML3E_COUNT || PdIndex >= EPT_PML2E_COUNT)
	{
		LogError("Not Expected Physical Address 0x%llx", PhysicalAddress);
		return NULL;
	}

	PEPT_PDE_2MB Pde2Mb = &Ept::EptState.PdTables[Pml4Index * EPT_PML3E_COUNT + PdptIndex].Entries[PdIndex];

	// If already split, return existing page table entry
	if (Pde2Mb->Fields.LargePage == 0)
	{
		PEPT_PDE Pde = (PEPT_PDE)Pde2Mb;
		ULONG64 PtePhysicalAddress = (ULONG64)Pde->Fields.PtePhysicalAddress << 12;

		// Find virtual address from tracking list
		PSPLIT_PAGE_NODE CurrentNode = Ept::EptState.SplitPageListHead;
		while (CurrentNode != NULL)
		{
			if (VirtualMemory::VirtualAddressToPhysicalAddress(CurrentNode->PageTableVirtualAddress) == PtePhysicalAddress)
			{
				PEPT_PTE PageTable = (PEPT_PTE)CurrentNode->PageTableVirtualAddress;
				ULONG64 PtIndex = EPT_PML1_INDEX(PhysicalAddress);
				return &PageTable[PtIndex];
			}
			CurrentNode = CurrentNode->Next;
		}
		return NULL;
	}

	PEPT_PTE NewPageTable = (PEPT_PTE)PhysicalMemory::AllocateContiguousMemory(PAGE_SIZE);
	if (NewPageTable == NULL)
	{
		LogError("Failed to allocate 4KB Page Table for splitting 2MB page");
		return NULL;
	}
	RtlZeroMemory(NewPageTable, PAGE_SIZE);

	ULONG64 NewTablePhysicalAddress = VirtualMemory::VirtualAddressToPhysicalAddress(NewPageTable);
	ULONG64 BasePhysicalAddress = PhysicalAddress & ~0x1FFFFFULL; // 2MB aligned

	for (ULONG i = 0; i < 512; i++)
	{
		NewPageTable[i].Fields.Read = 1;
		NewPageTable[i].Fields.Write = 1;
		NewPageTable[i].Fields.Execute = 1;
		NewPageTable[i].Fields.MemoryType = 6; // Write-Back (WB)
		NewPageTable[i].Fields.IgnorePat = 0;
		NewPageTable[i].Fields.PageFrameNumber = (BasePhysicalAddress + (i * PAGE_SIZE)) >> 12;
	}

	PSPLIT_PAGE_NODE Node = NULL;
	if (!VirtualMemory::AllocateNonPagedPool((PVOID*)&Node, sizeof(SPLIT_PAGE_NODE), FALSE))
	{
		PhysicalMemory::FreeContiguousMemory(NewPageTable);
		LogError("Failed to allocate split page tracking node");
		return NULL;
	}

	Node->PageTableVirtualAddress = NewPageTable;
	Node->Next = Ept::EptState.SplitPageListHead;
	Ept::EptState.SplitPageListHead = Node;

	PEPT_PDE Pde = (PEPT_PDE)Pde2Mb;
	Pde->Value = 0;
	Pde->Fields.Read = 1;
	Pde->Fields.Write = 1;
	Pde->Fields.Execute = 1;
	Pde->Fields.LargePage = 0; // Not a large page anymore
	Pde->Fields.PtePhysicalAddress = NewTablePhysicalAddress >> 12;

	if (Vcpu::VcpuState && Vcpu::VcpuState[0].VmcsLaunchStatus)
	{
		KeIpiGenericCall(Ept::InveptAllContexts, 0);
	}

	ULONG64 PtIndex = EPT_PML1_INDEX(PhysicalAddress);
	return &NewPageTable[PtIndex];
}

PEPT_PTE
Ept::GetPte(ULONG64 PhysicalAddress)
{
	if (PhysicalAddress >= EPT_MAPPED_SIZE || Ept::EptState.PdTables == NULL)
	{
		LogError("PhysicalAddress 0x%llx exceeds mapped 0x%llx range", PhysicalAddress, EPT_MAPPED_SIZE);
		return NULL;
	}

	ULONG64 Pml4Index = EPT_PML4_INDEX(PhysicalAddress);
	ULONG64 PdptIndex = EPT_PML3_INDEX(PhysicalAddress);
	ULONG64 PdIndex = EPT_PML2_INDEX(PhysicalAddress);

	if (Pml4Index >= EPT_PML4E_COUNT || PdptIndex >= EPT_PML3E_COUNT || PdIndex >= EPT_PML2E_COUNT)
	{
		LogError("Not Expected Physical Address 0x%llx", PhysicalAddress);
		return NULL;
	}

	PEPT_PDE_2MB Pde2Mb = &Ept::EptState.PdTables[Pml4Index * EPT_PML3E_COUNT + PdptIndex].Entries[PdIndex];

	// If not split yet, split it now
	if (Pde2Mb->Fields.LargePage == 1)
	{
		return Ept::SplitLargePageTo4Kb(PhysicalAddress);
	}

	// Already split, find the PTE
	PEPT_PDE Pde = (PEPT_PDE)Pde2Mb;
	ULONG64 PtePhysicalAddress = (ULONG64)Pde->Fields.PtePhysicalAddress << 12;

	PSPLIT_PAGE_NODE CurrentNode = Ept::EptState.SplitPageListHead;
	while (CurrentNode != NULL)
	{
		if (VirtualMemory::VirtualAddressToPhysicalAddress(CurrentNode->PageTableVirtualAddress) == PtePhysicalAddress)
		{
			PEPT_PTE PageTable = (PEPT_PTE)CurrentNode->PageTableVirtualAddress;
			ULONG64 PtIndex = EPT_PML1_INDEX(PhysicalAddress);
			return &PageTable[PtIndex];
		}
		CurrentNode = CurrentNode->Next;
	}

	LogError("Cannot find split page table for physical address 0x%llx", PhysicalAddress);

	return NULL;
}

BOOLEAN
Ept::HandleEptViolation(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress, ULONG64 ExitQualification)
{
	UNREFERENCED_PARAMETER(Context);
	UNREFERENCED_PARAMETER(GuestPhysicalAddress);
	UNREFERENCED_PARAMETER(ExitQualification);

	EPT_VIOLATION_QUALIFICATION Qualification = { 0 };
	Qualification.Value = ExitQualification;

	// Check if this violation belongs to an active EPT hook
	if (EptHook::HandleEptViolation(Context, GuestPhysicalAddress, Qualification))
	{
		return TRUE;
	}

	LogInformation("EPT Violation at GPA: 0x%llx [Read: %u, Write: %u, Execute: %u]",
		GuestPhysicalAddress,
		Qualification.Fields.ReadAccess,
		Qualification.Fields.WriteAccess,
		Qualification.Fields.ExecuteAccess);

	return FALSE;
}

BOOLEAN
Ept::HandleEptMisconfiguration(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress)
{
	UNREFERENCED_PARAMETER(Context);
	UNREFERENCED_PARAMETER(GuestPhysicalAddress);

	LogError("EPT Misconfiguration at GPA: 0x%llx! Check EPTP and EPT entry format.", GuestPhysicalAddress);
	return FALSE;
}
