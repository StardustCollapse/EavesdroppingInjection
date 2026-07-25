#include "Vmx/Vmx.h"
#include "Vmcs/Vmcs.h"
#include "Msr/MsrControlField.h"
#include "Vmcs/SegmentRegister.h"
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
Vmcs::VmxSetupVmcs(PVOID GuestStack)
{
	Vmcs::VmxSetupGuestState(GuestStack);

	__vmx_vmwrite(Host_State_Fields::Host_ES_Selector, AsmGetEs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_CS_Selector, AsmGetCs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_SS_Selector, AsmGetSs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_DS_Selector, AsmGetDs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_FS_Selector, AsmGetFs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_GS_Selector, AsmGetGs() & 0xFFF8);
	__vmx_vmwrite(Host_State_Fields::Host_TR_Selector, AsmGetTr() & 0xFFF8);

	__vmx_vmwrite(Control_Fields::TSC_offset_Full, 0);

	__vmx_vmwrite(Control_Fields::Page_Fault_Error_Code_Mask, 0);
	__vmx_vmwrite(Control_Fields::Page_Fault_Error_Code_Match, 0);

	__vmx_vmwrite(Control_Fields::VM_Exit_MSR_Store_Count, 0);
	__vmx_vmwrite(Control_Fields::VM_Exit_MSR_Load_Count, 0);

	__vmx_vmwrite(Control_Fields::VM_Entry_MSR_Load_Count, 0);
	__vmx_vmwrite(Control_Fields::VM_Entry_Interruption_Information_Field, 0);



	//InComplete
	return;
}

VOID
Vmcs::VmxSetupGuestState(PVOID GuestStack)
{
	__vmx_vmwrite(Guest_State_Fields::Guest_CR0, __readcr0());
	__vmx_vmwrite(Guest_State_Fields::Guest_CR3, __readcr3());
	__vmx_vmwrite(Guest_State_Fields::Guest_CR4, __readcr4());

	//DR7 Bit 10 is reserved, need to be set to 1
	__vmx_vmwrite(Guest_State_Fields::Guest_DR7, Util::Bit(10));

	//RIP in VM-EXIT must add bias to point to the next instruction
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
	//use for Linear Address Calculation
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

	//set shadow VMCS off -> No  Nested Virtualization
	__vmx_vmwrite(Guest_State_Fields::VMCS_Link_Pointer_Full, ~0ULL);

	return;
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