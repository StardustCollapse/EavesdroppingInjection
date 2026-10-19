#include "Vmx/Vmx.h"
#include "Vmcs/Vmcs.h"
#include "Msr/Msr.h"
#include "Msr/MsrControlField.h"
#include "Vmcs/SegmentRegister.h"
#include "VmExit/VmExit.h"
#include "Log/Log.h"
#include "Util.h"
#include "Ept/Ept.h"

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

	LogInformation("VMCS Clear Successfully ");

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


BOOLEAN
Vmcs::VmxSetupVmcs(PVCPU_STATE CurrentCPU, PVOID GuestStack)
{
	if(!Vmcs::VmxSetupGuestState(GuestStack))
	{
		LogError("Failed to Setup Guest State");
		return FALSE;
	}

	if (!Vmcs::VmxSetupHostState(CurrentCPU))
	{
		LogError("Failed to Setup Host State");
		return FALSE;
	}

	if(!Vmcs::VmxSetupControls(CurrentCPU))
	{
		LogError("Failed to Setup VMCS Controls");
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
Vmcs::VmxSetupGuestState(PVOID GuestStack)
{
	__vmx_vmwrite(Guest_State_Fields::Guest_CR0, __readcr0());
	__vmx_vmwrite(Guest_State_Fields::Guest_CR3, __readcr3());
	__vmx_vmwrite(Guest_State_Fields::Guest_CR4, __readcr4());

	// DR7: Read current DR7 and ensure bit 10 is 1 (reserved)
	__vmx_vmwrite(Guest_State_Fields::Guest_DR7, __readdr(7) | Util::Bit(10));

	__vmx_vmwrite(Guest_State_Fields::Guest_RSP, (ULONG64)GuestStack);
	__vmx_vmwrite(Guest_State_Fields::Guest_RIP, (ULONG64)AsmVmxContextRestoreState);
	__vmx_vmwrite(Guest_State_Fields::Guest_RFLAGS, AsmGetRflags());

	ULONG64 GdtBase = AsmGetGdtBase();
	if(!LoadGuestSegment(GdtBase, AsmGetEs(), SegmentRegisterIndex::ES))
	{
		LogError("Failed to Load Guest Segment ES");
		return FALSE;
	}
	if(!LoadGuestSegment(GdtBase, AsmGetCs(), SegmentRegisterIndex::CS))
	{
		LogError("Failed to Load Guest Segment CS");
		return FALSE;
	}
	if(!LoadGuestSegment(GdtBase, AsmGetSs(), SegmentRegisterIndex::SS))
	{
		LogError("Failed to Load Guest Segment SS");
		return FALSE;
	}
	if(!LoadGuestSegment(GdtBase, AsmGetDs(), SegmentRegisterIndex::DS))
	{
		LogError("Failed to Load Guest Segment DS");
		return FALSE;
	}
	if(!LoadGuestSegment(GdtBase, AsmGetFs(), SegmentRegisterIndex::FS))
	{
		LogError("Failed to Load Guest Segment FS");
		return FALSE;
	}
	if(!LoadGuestSegment(GdtBase, AsmGetGs(), SegmentRegisterIndex::GS))
	{
		LogError("Failed to Load Guest Segment GS");
		return FALSE;
	}
	if(!LoadGuestSegment(GdtBase, AsmGetLdtr(), SegmentRegisterIndex::LDTR))
	{
		LogError("Failed to Load Guest Segment LDTR");
		return FALSE;
	}
	if(!LoadGuestSegment(GdtBase, AsmGetTr(), SegmentRegisterIndex::TR))
	{
		LogError("Failed to Load Guest Segment TR");
		return FALSE;
	}

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
	return TRUE;
}

BOOLEAN
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

	__vmx_vmwrite(Host_State_Fields::Host_CR0, __readcr0());
	//__vmx_vmwrite(Host_State_Fields::Host_CR3, __readcr3());
	__vmx_vmwrite(Host_State_Fields::Host_CR3, Util::HostCr3);
	__vmx_vmwrite(Host_State_Fields::Host_CR4, __readcr4());

	__vmx_vmwrite(Host_State_Fields::Host_FS_Base, __readmsr(MSRs::IA32_FS_BASE));
	__vmx_vmwrite(Host_State_Fields::Host_GS_Base, __readmsr(MSRs::IA32_GS_BASE));
	ULONG64 TrBase = Vmcs::GetSegmentBase(AsmGetGdtBase(), AsmGetTr());
	if (!TrBase)
	{
		return FALSE;
	}
	__vmx_vmwrite(Host_State_Fields::Host_TR_Base, TrBase);
	__vmx_vmwrite(Host_State_Fields::Host_GDTR_Base, AsmGetGdtBase());
	__vmx_vmwrite(Host_State_Fields::Host_IDTR_Base, AsmGetIdtBase());

	__vmx_vmwrite(Host_State_Fields::Host_IA32_SYSENTER_CS, __readmsr(MSRs::IA32_SYSENTER_CS));
	__vmx_vmwrite(Host_State_Fields::Host_IA32_SYSENTER_ESP, __readmsr(MSRs::IA32_SYSENTER_ESP));
	__vmx_vmwrite(Host_State_Fields::Host_IA32_SYSENTER_EIP, __readmsr(MSRs::IA32_SYSENTER_EIP));

	// Top of VmmStack aligned to 16 bytes
	ULONG64 HostRsp = CurrentCPU->VmmStack + VMM_STACK_SIZE - PAGE_SIZE;
	__vmx_vmwrite(Host_State_Fields::Host_RSP, HostRsp);

	// VM-Exit handler entry point
	__vmx_vmwrite(Host_State_Fields::Host_RIP, (ULONG64)AsmVmExitHandler);
	return TRUE;
}

BOOLEAN
Vmcs::VmxSetupControls(PVCPU_STATE CurrentCPU)
{
	BOOLEAN UseTrueMsrs = (Msr::IA32VmxBasic.Fields.VmxCapabilityHint != 0);

	ULONG PinBasedControls;
	if (!Vmcs::AdjustControl(0, UseTrueMsrs ? MSRs::IA32_VMX_TRUE_PINBASED_CTLS : MSRs::IA32_VMX_PINBASED_CTLS, &PinBasedControls))
	{
		LogError("PINBASED_CTLS MSR Not Support Capability");
		return FALSE;
	}
	__vmx_vmwrite(Control_Fields::Pin_Based_VM_Execution_Controls, PinBasedControls);

	ULONG PrimaryProcMsr = UseTrueMsrs ? MSRs::IA32_VMX_TRUE_PROCBASED_CTLS : MSRs::IA32_VMX_PROCBASED_CTLS;

	// Verify CPU supports Monitor Trap Flag capability upfront
	MSR_INFORMATION PrimaryProcMsrInfo;
	PrimaryProcMsrInfo.QuadPart = __readmsr(PrimaryProcMsr);
	if (!(PrimaryProcMsrInfo.HighPart & VM_Execution_Controls::PRIMARY_PROCESSOR_BASED_MONITOR_TRAP_FLAG))
	{
		LogError("PRIMARY_PROCESSOR_BASED_MONITOR_TRAP_FLAG Not Support Capability");
		return FALSE;
	}

	ULONG PrimaryProcControls = VM_Execution_Controls::PRIMARY_PROCESSOR_BASED_ACTIVATE_SECONDARY_CONTROLS | VM_Execution_Controls::PRIMARY_PROCESSOR_BASED_USE_MSR_BITMAPS;
	if (!Vmcs::AdjustControl(PrimaryProcControls, PrimaryProcMsr, &PrimaryProcControls))
	{
		LogError("PROCBASED_CTLS MSR Not Support Capability");
		return FALSE;
	}
	CurrentCPU->PrimaryProcControls = PrimaryProcControls;
	CurrentCPU->PrimaryProcControlsWithMtf = PrimaryProcControls | VM_Execution_Controls::PRIMARY_PROCESSOR_BASED_MONITOR_TRAP_FLAG;
	__vmx_vmwrite(Control_Fields::Primary_Processor_Based_VM_Execution_Controls, PrimaryProcControls);

	ULONG SecondaryProcControls = VM_Execution_Controls::SECONDARY_PROCESSOR_BASED_ENABLE_RDTSCP | VM_Execution_Controls::SECONDARY_PROCESSOR_BASED_ENABLE_INVPCID;
	if (Ept::GetEptPointer().Value != 0)
	{
		SecondaryProcControls |= VM_Execution_Controls::SECONDARY_PROCESSOR_BASED_ENABLE_EPT;
	}
	MSR_INFORMATION MsrSecProcControl;
	MsrSecProcControl.QuadPart = __readmsr(MSRs::IA32_VMX_PROCBASED_CTLS2);
	if ((MsrSecProcControl.HighPart & VM_Execution_Controls::SECONDARY_PROCESSOR_BASED_XSAVES) && Vmx::cpuid.ecx.ECX_01H.XSAVE && (__readcr4() & Util::Bit(18)))
	{
		// Enable XSAVES if supported, Cr4 bit 18 -> OSXSAVE
		SecondaryProcControls |= VM_Execution_Controls::SECONDARY_PROCESSOR_BASED_XSAVES;
	}
	if (!Vmcs::AdjustControl(SecondaryProcControls, MSRs::IA32_VMX_PROCBASED_CTLS2, &SecondaryProcControls))
	{
		LogError("PROCBASED_CTLS2 MSR Not Support Capability");
		return FALSE;
	}
	__vmx_vmwrite(Control_Fields::Secondary_Processor_Based_VM_Execution_Controls, SecondaryProcControls);

	// Host Address Space Size must be 1 for 64-bit host
	ULONG ExitControls = VM_Exit_Controls::VM_EXIT_HOST_ADDRESS_SPACE_SIZE;
	if (!Vmcs::AdjustControl(ExitControls, UseTrueMsrs ? MSRs::IA32_VMX_TRUE_EXIT_CTLS : MSRs::IA32_VMX_EXIT_CTLS, &ExitControls))
	{
		LogError("EXIT_CTLS MSR Not Support Capability");
		return FALSE;
	}
	__vmx_vmwrite(Control_Fields::Primary_VM_Exit_Controls, ExitControls);

	// IA-32e mode guest must be 1 for 64-bit guest
	ULONG EntryControls = VM_Entry_Controls::VM_ENTRY_IA32E_MODE;
	if (!Vmcs::AdjustControl(EntryControls, UseTrueMsrs ? MSRs::IA32_VMX_TRUE_ENTRY_CTLS : MSRs::IA32_VMX_ENTRY_CTLS, &EntryControls))
	{
		LogError("ENTRY_CTLS MSR Not Support Capability");
		return FALSE;
	}
	__vmx_vmwrite(Control_Fields::VM_Entry_Controls, EntryControls);

	__vmx_vmwrite(Control_Fields::Address_of_MSR_Bitmaps_Full, CurrentCPU->MsrBitmapPhysicalAddress);

	if (Ept::GetEptPointer().Value != 0)
	{
		__vmx_vmwrite(Control_Fields::EPT_Pointer_Full, Ept::GetEptPointer().Value);
	}

	__vmx_vmwrite(Control_Fields::CR0_Guest_Host_Mask, 0 /*Util::Bit(0) | Util::Bit(31)*/);
	__vmx_vmwrite(Control_Fields::CR4_Guest_Host_Mask, Util::Bit(13));
	__vmx_vmwrite(Control_Fields::CR0_Read_Shadow, __readcr0());
	__vmx_vmwrite(Control_Fields::CR4_Read_Shadow, __readcr4() & ~Util::Bit(13));

	__vmx_vmwrite(Control_Fields::CR3_Target_Count, 0);
	__vmx_vmwrite(Control_Fields::Exception_Bitmap, 0);
	__vmx_vmwrite(Control_Fields::TSC_offset_Full, 0);
	__vmx_vmwrite(Control_Fields::XSS_Exiting_Bitmap_Full, 0);

	__vmx_vmwrite(Control_Fields::Page_Fault_Error_Code_Mask, 0);
	__vmx_vmwrite(Control_Fields::Page_Fault_Error_Code_Match, 0);

	__vmx_vmwrite(Control_Fields::VM_Exit_MSR_Store_Count, 0);
	__vmx_vmwrite(Control_Fields::VM_Exit_MSR_Load_Count, 0);

	__vmx_vmwrite(Control_Fields::VM_Entry_MSR_Load_Count, 0);
	__vmx_vmwrite(Control_Fields::VM_Entry_Interruption_Information_Field, 0);

	return TRUE;
}

ULONG64
Vmcs::GetSegmentBase(ULONG64 GdtBase, USHORT Selector)
{
	SEGMENT_SELECTOR SegmentSelector = { 0 };
	SegmentSelector.Value = Selector;

	if (SegmentSelector.Bits.TI || !Selector)
	{
		return FALSE;
	}

	PSEGMENT_DESCRIPTORS SegmentDescriptor = (PSEGMENT_DESCRIPTORS)(GdtBase + (SegmentSelector.Bits.Index << 3));
	ULONG64 Base = ((ULONG64)SegmentDescriptor->Descriptor.Base1) | ((ULONG64)SegmentDescriptor->Descriptor.Base2 << 16) | ((ULONG64)SegmentDescriptor->Descriptor.Base3 << 24);

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

	if (SegmentRegister == SegmentRegisterIndex::FS || SegmentRegister == SegmentRegisterIndex::GS)
	{
		SegmentInformation.Selector = Selector;
		SegmentInformation.Base = __readmsr((SegmentRegister == SegmentRegisterIndex::FS) ? MSRs::IA32_FS_BASE : MSRs::IA32_GS_BASE);
		SegmentInformation.Limit = Selector ? __segmentlimit(Selector) : 0xFFFFFFFF;
		if (Selector)
		{
			PSEGMENT_DESCRIPTORS SegmentDescriptor = (PSEGMENT_DESCRIPTORS)(GdtBase + (SegmentSelector.Bits.Index << 3));
			SegmentInformation.AccessRights = (((ULONG)SegmentDescriptor->Descriptor.AccessRights1.Value) | (((ULONG)SegmentDescriptor->Descriptor.Limit2AccessRights2.Value.AccessRights) << 12));
		}
		else
		{
			//Unusable(16) = 0, DPL(6:5) = 0, P(4) = 1, Type(3:0) = 3 (Data_Read/Write,Accessed)
			SegmentInformation.AccessRights = (ULONG)Util::Bit(16);
		}
		goto WriteVmcsField;
	}

	if (!Selector || ((SegmentRegister == SegmentRegisterIndex::SS) && !(Selector & 0xFFFC)))
	{
		//a segment register is unusable if it has been loaded with a null selector
		SegmentInformation.Selector = Selector;
		SegmentInformation.Base = 0;
		SegmentInformation.Limit = 0;
		//Unusable(16) = 0, DPL(6:5) = 0, P(4) = 1, Type(3:0) = 3 (Data_Read/Write,Accessed)
		SegmentInformation.AccessRights = (SegmentRegister == SegmentRegisterIndex::SS) ? 0x93 : (ULONG)Util::Bit(16);
		goto WriteVmcsField;
	}

	PSEGMENT_DESCRIPTORS SegmentDescriptor = (PSEGMENT_DESCRIPTORS)(GdtBase + (SegmentSelector.Bits.Index << 3));
	SegmentInformation.Selector = SegmentSelector.Value;
	SegmentInformation.Limit = ((ULONG)SegmentDescriptor->Descriptor.Limit1) | (((ULONG)SegmentDescriptor->Descriptor.Limit2AccessRights2.Value.Limit) << 16);
	SegmentInformation.AccessRights = (((ULONG)SegmentDescriptor->Descriptor.AccessRights1.Value) | (((ULONG)SegmentDescriptor->Descriptor.Limit2AccessRights2.Value.AccessRights)<< 12));
	SegmentInformation.Base = ((ULONG64)SegmentDescriptor->Descriptor.Base1) | (((ULONG64)SegmentDescriptor->Descriptor.Base2) << 16) | (((ULONG64)SegmentDescriptor->Descriptor.Base3) << 24);

	if (!SegmentDescriptor->Descriptor.AccessRights1.Bits.S)
	{
		SegmentInformation.Base |= (((ULONG64)SegmentDescriptor->Descriptor.Base4) << 32);
	}

	if (SegmentDescriptor->Descriptor.Limit2AccessRights2.Bits.G)
	{
		SegmentInformation.Limit = ((SegmentInformation.Limit << 12) | (PAGE_SIZE - 1));
	}

WriteVmcsField:
	__vmx_vmwrite(GuestSegmentSelectorTransform(SegmentRegister), SegmentInformation.Selector);
	__vmx_vmwrite(GuestSegmentLimitTransform(SegmentRegister), SegmentInformation.Limit);
	__vmx_vmwrite(GuestSegmentAccessRightsTransform(SegmentRegister), SegmentInformation.AccessRights);
	__vmx_vmwrite(GuestSegmentBaseTransform(SegmentRegister), SegmentInformation.Base);

	return TRUE;
}

BOOLEAN
Vmcs::AdjustControl(ULONG DesiredValue, ULONG MsrIndex, PULONG ActualValue)
{
	if (!ActualValue) return FALSE;

	MSR_INFORMATION MsrValue;
	MsrValue.QuadPart = __readmsr(MsrIndex);

	ULONG AdjustedValue = DesiredValue;

	if ((AdjustedValue & MsrValue.HighPart) != DesiredValue)
	{
		return FALSE;
	}

	*ActualValue = AdjustedValue | MsrValue.LowPart;

	return TRUE;
}

BOOLEAN
Vmcs::EnableMonitorTrapFlag()
{
	ULONG CurrentCpu = KeGetCurrentProcessorNumberEx(NULL);
	UCHAR Status = __vmx_vmwrite(Control_Fields::Primary_Processor_Based_VM_Execution_Controls, Vcpu::VcpuState[CurrentCpu].PrimaryProcControlsWithMtf);
	if (Status != 0)
	{
		LogError("Failed to enable MTF in VMCS. Error code: %u", Status);
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
Vmcs::DisableMonitorTrapFlag()
{
	ULONG CurrentCpu = KeGetCurrentProcessorNumberEx(NULL);
	UCHAR Status = __vmx_vmwrite(Control_Fields::Primary_Processor_Based_VM_Execution_Controls, Vcpu::VcpuState[CurrentCpu].PrimaryProcControls);
	if (Status != 0)
	{
		LogError("Failed to disable MTF in VMCS. Error code: %u", Status);
		return FALSE;
	}

	return TRUE;
}