#include "Vmx/Vmx.h"
#include "Vmcs/Vmcs.h"
#include "Msr/Msr.h"
#include "Msr/MsrControlField.h"
#include "Vmcs/SegmentRegister.h"
#include "VmExit/VmExit.h"
#include "Log/Log.h"
#include "Util.h"

BOOLEAN
Vmcs::VmxClearVmcs(PVCPU_STATE CurrentCPU)
{
	CurrentCPU->VmcsClearStatus = FALSE;
	UCHAR VmclearStatus = 3;

	VmclearStatus = __vmx_vmclear(&CurrentCPU->VmcsRegionPhysicalAddress);

	if (VmclearStatus)
	{
		LogError("Failed to Clear VMCS. Error code: %d", VmclearStatus);
		return FALSE;
	}
	CurrentCPU->VmcsClearStatus = TRUE;

	LogInformation("VMCS Clear Success");

	return TRUE;
}

BOOLEAN
Vmcs::VmxLoadVmcs(PVCPU_STATE CurrentCPU)
{
	CurrentCPU->VmcsLoadStatus = FALSE;
	UCHAR VmptrldStatus = 3;

	VmptrldStatus = __vmx_vmptrld(&CurrentCPU->VmcsRegionPhysicalAddress);

	if (VmptrldStatus)
	{
		LogError("Failed to Load VMCS");
		return FALSE;
	}

	CurrentCPU->VmcsLoadStatus = TRUE;

	LogInformation("VMCS Load Successfully");

	return TRUE;
}

VOID
Vmcs::VmxSetupControls(PVCPU_STATE CurrentCPU)
{
	BOOLEAN UseTrueMsrs = (Msr::IA32VmxBasic.Fields.VmxCapabilityHint != 0);

	// 1. Pin-Based VM-Execution Controls
	ULONG PinBasedControls = Vmcs::AdjustControl(0, UseTrueMsrs ? MSRs::IA32_VMX_TRUE_PINBASED_CTLS : MSRs::IA32_VMX_PINBASED_CTLS);
	__vmx_vmwrite(Control_Fields::Pin_Based_VM_Execution_Controls, PinBasedControls);

	// 2. Primary Processor-Based VM-Execution Controls
	ULONG PrimaryProcControls = VM_Execution_Controls::CPU_BASED_ACTIVATE_SECONDARY_CONTROLS | VM_Execution_Controls::CPU_BASED_USE_MSR_BITMAPS;
	PrimaryProcControls = Vmcs::AdjustControl(PrimaryProcControls, UseTrueMsrs ? MSRs::IA32_VMX_TRUE_PROCBASED_CTLS : MSRs::IA32_VMX_PROCBASED_CTLS);
	__vmx_vmwrite(Control_Fields::Primary_Processor_Based_VM_Execution_Controls, PrimaryProcControls);

	// 3. Secondary Processor-Based VM-Execution Controls
	ULONG SecondaryProcControls = VM_Execution_Controls::SECONDARY_EXEC_ENABLE_RDTSCP | VM_Execution_Controls::SECONDARY_EXEC_ENABLE_INVPCID;	//SECONDARY_EXEC_DESCRIPTOR_TABLE_EXIT, SECONDARY_EXEC_ENABLE_EPT
	if (Vmx::cpuid.ecx.ECX_01H.XSAVE && (__readcr4() & Util::Bit(18)))
	{
		SecondaryProcControls |= VM_Execution_Controls::SECONDARY_EXEC_XSAVES;
	}
	SecondaryProcControls = Vmcs::AdjustControl(SecondaryProcControls, MSRs::IA32_VMX_PROCBASED_CTLS2);
	__vmx_vmwrite(Control_Fields::Secondary_Processor_Based_VM_Execution_Controls, SecondaryProcControls);

	// 4. Primary VM-Exit Controls (Host Address Space Size must be 1 for 64-bit host)
	ULONG ExitControls = VM_Exit_Controls::VM_EXIT_HOST_ADDR_SPACE_SIZE;
	ExitControls = Vmcs::AdjustControl(ExitControls, UseTrueMsrs ? MSRs::IA32_VMX_TRUE_EXIT_CTLS : MSRs::IA32_VMX_EXIT_CTLS);
	__vmx_vmwrite(Control_Fields::Primary_VM_Exit_Controls, ExitControls);

	// 5. VM-Entry Controls (IA-32e mode guest must be 1 for 64-bit guest)
	ULONG EntryControls = VM_Entry_Controls::VM_ENTRY_IA32E_MODE;
	EntryControls = Vmcs::AdjustControl(EntryControls, UseTrueMsrs ? MSRs::IA32_VMX_TRUE_ENTRY_CTLS : MSRs::IA32_VMX_ENTRY_CTLS);
	__vmx_vmwrite(Control_Fields::VM_Entry_Controls, EntryControls);

	// 6. Address of MSR Bitmaps
	__vmx_vmwrite(Control_Fields::Address_of_MSR_Bitmaps_Full, CurrentCPU->MsrBitmapPhysicalAddress);

	// 7. CR Masks and Shadows
	__vmx_vmwrite(Control_Fields::CR0_Guest_Host_Mask, 0);
	__vmx_vmwrite(Control_Fields::CR4_Guest_Host_Mask, Util::Bit(13));
	__vmx_vmwrite(Control_Fields::CR0_Read_Shadow, __readcr0());
	__vmx_vmwrite(Control_Fields::CR4_Read_Shadow, __readcr4() & ~Util::Bit(13));

	// 8. Other Execution Controls
	__vmx_vmwrite(Control_Fields::CR3_Target_Count, 0);
	__vmx_vmwrite(Control_Fields::Exception_Bitmap, 0);
	__vmx_vmwrite(Control_Fields::TSC_offset_Full, 0);

	__vmx_vmwrite(Control_Fields::Page_Fault_Error_Code_Mask, 0);
	__vmx_vmwrite(Control_Fields::Page_Fault_Error_Code_Match, 0);

	__vmx_vmwrite(Control_Fields::VM_Exit_MSR_Store_Count, 0);
	__vmx_vmwrite(Control_Fields::VM_Exit_MSR_Load_Count, 0);

	__vmx_vmwrite(Control_Fields::VM_Entry_MSR_Load_Count, 0);
	__vmx_vmwrite(Control_Fields::VM_Entry_Interruption_Information_Field, 0);

	__vmx_vmwrite(Control_Fields::XSS_Exiting_Bitmap_Full, 0);
	return;
}

VOID
Vmcs::VmxSetupHostState(PVCPU_STATE CurrentCPU)
{
	// Host segment selectors (RPL must be 0)
	__vmx_vmwrite(Host_State_Fields::Host_ES_Selector, AsmGetEs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_CS_Selector, AsmGetCs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_SS_Selector, AsmGetSs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_DS_Selector, AsmGetDs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_FS_Selector, AsmGetFs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_GS_Selector, AsmGetGs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_TR_Selector, AsmGetTr() & 0xFFF8);

	// Host control registers
	__vmx_vmwrite(Host_State_Fields::Host_CR0, __readcr0());
	//__vmx_vmwrite(Host_State_Fields::Host_CR3, __readcr3());
	__vmx_vmwrite(Host_State_Fields::Host_CR3, Util::HostCr3);
	__vmx_vmwrite(Host_State_Fields::Host_CR4, __readcr4());

	// Host base addresses
	__vmx_vmwrite(Host_State_Fields::Host_FS_Base, __readmsr((ULONG)MSRs::IA32_FS_BASE));
	__vmx_vmwrite(Host_State_Fields::Host_GS_Base, __readmsr((ULONG)MSRs::IA32_GS_BASE));
	__vmx_vmwrite(Host_State_Fields::Host_TR_Base, Vmcs::GetSegmentBase(AsmGetGdtBase(), AsmGetTr()));
	__vmx_vmwrite(Host_State_Fields::Host_GDTR_Base, AsmGetGdtBase());
	__vmx_vmwrite(Host_State_Fields::Host_IDTR_Base, AsmGetIdtBase());

	// Host SYSENTER MSRs
	__vmx_vmwrite(Host_State_Fields::Host_IA32_SYSENTER_CS, __readmsr(MSRs::IA32_SYSENTER_CS));
	__vmx_vmwrite(Host_State_Fields::Host_IA32_SYSENTER_ESP, __readmsr(MSRs::IA32_SYSENTER_ESP));
	__vmx_vmwrite(Host_State_Fields::Host_IA32_SYSENTER_EIP, __readmsr(MSRs::IA32_SYSENTER_EIP));

	// Host RSP: Top of VmmStack aligned to 16 bytes
	ULONG64 HostRsp = CurrentCPU->VmmStack + VMM_STACK_SIZE - PAGE_SIZE;
	__vmx_vmwrite(Host_State_Fields::Host_RSP, HostRsp);

	// Host RIP: VM-Exit handler entry point
	__vmx_vmwrite(Host_State_Fields::Host_RIP, (ULONG64)AsmVmExitHandler);
	return;
}

VOID
Vmcs::VmxSetupGuestState(PVOID GuestStack)
{
	__vmx_vmwrite(Guest_State_Fields::Guest_CR0, __readcr0());
	__vmx_vmwrite(Guest_State_Fields::Guest_CR3, __readcr3());
	__vmx_vmwrite(Guest_State_Fields::Guest_CR4, __readcr4());

	// DR7: Read current DR7 and ensure bit 10 is 1 (reserved)
	__vmx_vmwrite(Guest_State_Fields::Guest_DR7, __readdr(7) | Util::Bit(10));

	// RSP and RIP
	__vmx_vmwrite(Guest_State_Fields::Guest_RSP, (ULONG64)GuestStack);
	__vmx_vmwrite(Guest_State_Fields::Guest_RIP, (ULONG64)AsmVmxContextRestoreState);
	__vmx_vmwrite(Guest_State_Fields::Guest_RFLAGS, AsmGetRflags());

	ULONG64 GdtBase = AsmGetGdtBase();
	LoadGuestSegment(GdtBase, AsmGetEs(), SegmentRegisterIndex::ES);
	LoadGuestSegment(GdtBase, AsmGetCs(), SegmentRegisterIndex::CS);
	LoadGuestSegment(GdtBase, AsmGetSs(), SegmentRegisterIndex::SS);
	LoadGuestSegment(GdtBase, AsmGetDs(), SegmentRegisterIndex::DS);
	LoadGuestSegment(GdtBase, AsmGetFs(), SegmentRegisterIndex::FS);
	LoadGuestSegment(GdtBase, AsmGetGs(), SegmentRegisterIndex::GS);
	LoadGuestSegment(GdtBase, AsmGetLdtr(), SegmentRegisterIndex::LDTR);
	LoadGuestSegment(GdtBase, AsmGetTr(), SegmentRegisterIndex::TR);

	// Base for FS and GS
	__vmx_vmwrite(Guest_State_Fields::Guest_FS_Base, __readmsr((ULONG)MSRs::IA32_FS_BASE));
	__vmx_vmwrite(Guest_State_Fields::Guest_GS_Base, __readmsr((ULONG)MSRs::IA32_GS_BASE));

	__vmx_vmwrite(Guest_State_Fields::Guest_GDTR_Base, AsmGetGdtBase());
	__vmx_vmwrite(Guest_State_Fields::Guest_GDTR_Limit, AsmGetGdtLimit());
	__vmx_vmwrite(Guest_State_Fields::Guest_IDTR_Base, AsmGetIdtBase());
	__vmx_vmwrite(Guest_State_Fields::Guest_IDTR_Limit, AsmGetIdtLimit());

	//to use debug, VM_Entry_Controls set entry
	__vmx_vmwrite(Guest_State_Fields::Guest_IA32_DEBUGCTL_Full, __readmsr(MSRs::IA32_DEBUGCTL));

	__vmx_vmwrite(Guest_State_Fields::Guest_IA32_SYSENTER_CS, __readmsr(MSRs::IA32_SYSENTER_CS));
	__vmx_vmwrite(Guest_State_Fields::Guest_IA32_SYSENTER_ESP, __readmsr(MSRs::IA32_SYSENTER_ESP));
	__vmx_vmwrite(Guest_State_Fields::Guest_IA32_SYSENTER_EIP, __readmsr(MSRs::IA32_SYSENTER_EIP));

	// Non-register guest state fields
	__vmx_vmwrite(Guest_State_Fields::Guest_Activity_State, 0);
	__vmx_vmwrite(Guest_State_Fields::Guest_Interruptibility_State, 0);
	__vmx_vmwrite(Guest_State_Fields::Guest_Pending_Debug_Exceptions, 0);

	// Disable shadow VMCS
	__vmx_vmwrite(Guest_State_Fields::VMCS_Link_Pointer_Full, ~0ULL);
	return;
}

VOID
Vmcs::VmxSetupVmcs(PVCPU_STATE CurrentCPU, PVOID GuestStack)
{
	Vmcs::VmxSetupGuestState(GuestStack);
	Vmcs::VmxSetupHostState(CurrentCPU);
	Vmcs::VmxSetupControls(CurrentCPU);
	return;
}

ULONG
Vmcs::AdjustControl(ULONG DesiredValue, ULONG MsrIndex)
{
	LARGE_INTEGER MsrValue;
	MsrValue.QuadPart = __readmsr(MsrIndex);

	// Bits that must be 1 (allowed 0-settings)
	DesiredValue |= MsrValue.LowPart;

	// Bits that must be 0 (allowed 1-settings)
	DesiredValue &= MsrValue.HighPart;

	return DesiredValue;
}

ULONG64
Vmcs::GetSegmentBase(ULONG64 GdtBase, USHORT Selector)
{
	SEGMENT_SELECTOR SegmentSelector = { 0 };
	SegmentSelector.Value = Selector;

	if (SegmentSelector.Bits.TI || !Selector)
	{
		return 0;
	}

	PSEGMENT_DESCRIPTORS SegmentDescriptor = (PSEGMENT_DESCRIPTORS)(GdtBase + (SegmentSelector.Bits.Index << 3));
	ULONG64 Base = (ULONG64)SegmentDescriptor->Descriptor.Base1 |
		((ULONG64)SegmentDescriptor->Descriptor.Base2 << 16) |
		((ULONG64)SegmentDescriptor->Descriptor.Base3 << 24);

	if (!SegmentDescriptor->Descriptor.AccessRights1.Bits.S)
	{
		Base |= ((ULONG64)SegmentDescriptor->Descriptor.Base4 << 32);
	}

	return Base;
}

BOOLEAN
Vmcs::LoadGuestSegment(ULONG64 GdtBase, USHORT Selector, UCHAR SegmentRegister)
{
	SEGMENT_INFORMATION SegmentInformation = { 0 };
	SEGMENT_SELECTOR SegmentSelector = { 0 };

	SegmentSelector.Value = Selector;
	if (SegmentSelector.Bits.TI)
	{
		return FALSE;
	}

	PSEGMENT_DESCRIPTORS SegmentDescriptor = (PSEGMENT_DESCRIPTORS)(GdtBase + (SegmentSelector.Bits.Index << 3));
	SegmentInformation.Selector = SegmentSelector.Value;
	SegmentInformation.Limit = (ULONG)SegmentDescriptor->Descriptor.Limit1 | (ULONG)SegmentDescriptor->Descriptor.Limit2AccessRights2.Value.Limit << 16;
	SegmentInformation.AccessRights = (ULONG)SegmentDescriptor->Descriptor.AccessRights1.Value | (ULONG)SegmentDescriptor->Descriptor.Limit2AccessRights2.Value.AccessRights << 12;
	SegmentInformation.Base = (ULONG64)SegmentDescriptor->Descriptor.Base1 | (ULONG64)SegmentDescriptor->Descriptor.Base2 << 16 | (ULONG64)SegmentDescriptor->Descriptor.Base3 << 24;

	if (!SegmentDescriptor->Descriptor.AccessRights1.Bits.S)
	{
		SegmentInformation.Base |= (ULONG64)SegmentDescriptor->Descriptor.Base4 << 32;
	}

	if (SegmentDescriptor->Descriptor.Limit2AccessRights2.Bits.G)
	{
		SegmentInformation.Limit = (SegmentInformation.Limit << 12) | (PAGE_SIZE - 1);
	}

	if (!Selector)
	{
		//a segment register is unusable if it has been loaded with a null selector
		SegmentInformation.AccessRights |= Util::Bit(16);
	}

	__vmx_vmwrite(GuestSegmentSelectorTransform(SegmentRegister), SegmentInformation.Selector);
	__vmx_vmwrite(GuestSegmentLimitTransform(SegmentRegister), SegmentInformation.Limit);
	__vmx_vmwrite(GuestSegmentAccessRightsTransform(SegmentRegister), SegmentInformation.AccessRights);
	__vmx_vmwrite(GuestSegmentBaseTransform(SegmentRegister), SegmentInformation.Base);

	return TRUE;
}