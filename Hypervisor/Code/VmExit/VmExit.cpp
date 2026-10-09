#include "Vmx/SupportVmx.h"
#include "VmExit/VmExit.h"
#include "VmExit/InjectEvent.h"
#include "VmExit/VmxBasicExitReasons.h"
#include "VmCall/VmCall.h"
#include "Vmcs/VmcsField.h"
#include "Msr/MsrControlField.h"
#include "Util.h"
#include "Log/Log.h"
#include "Ept/Ept.h"
#include <intrin.h>

BOOLEAN
VmExit::VmExitDispatcher(PGUEST_CONTEXT Context)
{
	BOOLEAN SkipGuestRipAdvance = FALSE;

	ULONG64 ExitReason = 0;
	__vmx_vmread(Read_Only_Data_Fields::Exit_Reason, &ExitReason);

	// Bits 15:0 of this field contain the basic exit reason.
	USHORT BasicExitReason = (USHORT)(ExitReason & 0xFFFF);

	switch (BasicExitReason)
	{
	case EXIT_REASONS::EXIT_REASON_CPUID:
		VmExit::HandleCpuid(Context);
		break;

	case EXIT_REASONS::EXIT_REASON_VMCALL:
		VmExit::HandleVmCall(Context);
		break;

	case EXIT_REASONS::EXIT_REASON_INVD:
		__wbinvd();
		break;

	case EXIT_REASONS::EXIT_REASON_RDMSR:
		if (!VmExit::HandleRdmsr(Context)) SkipGuestRipAdvance = TRUE;
		break;

	case EXIT_REASONS::EXIT_REASON_WRMSR:
		if (!VmExit::HandleWrmsr(Context)) SkipGuestRipAdvance = TRUE;
		break;

	case EXIT_REASONS::EXIT_REASON_XSETBV:
		_xsetbv((ULONG)Context->Rcx, ((Context->Rdx & 0xFFFFFFFF) << 32) | (Context->Rax & 0xFFFFFFFF));
		break;

	case EXIT_REASONS::EXIT_REASON_EPT_VIOLATION:
	{
		SkipGuestRipAdvance = TRUE;
		ULONG64 GuestPhysicalAddress = 0;
		ULONG64 ExitQualification = 0;
		__vmx_vmread(Read_Only_Data_Fields::Guest_Physical_Address_Full, &GuestPhysicalAddress);
		__vmx_vmread(Read_Only_Data_Fields::Exit_Qualification, &ExitQualification);
		if (!Ept::HandleEptViolation(Context, GuestPhysicalAddress, ExitQualification))
		{
			LogError("Unhandled EPT Violation at GPA: 0x%llx, Qualification: 0x%llx", GuestPhysicalAddress, ExitQualification);
		}
		break;
	}

	case EXIT_REASONS::EXIT_REASON_EPT_MISCONFIGURATION:
	{
		SkipGuestRipAdvance = TRUE;
		ULONG64 GuestPhysicalAddress = 0;
		__vmx_vmread(Read_Only_Data_Fields::Guest_Physical_Address_Full, &GuestPhysicalAddress);
		Ept::HandleEptMisconfiguration(Context, GuestPhysicalAddress);
		break;
	}

	default:
		LogError("Unhandled VM-Exit reason: %u (0x%llx)", BasicExitReason, ExitReason);
		break;
	}

	//LogInformation("VM-Exit handled. Reason: %u (0x%llX)", BasicExitReason, ExitReason);

	if (!SkipGuestRipAdvance)
	{
		VmExit::AdvanceGuestRip();
	}
	return FALSE;
}

EXTERN_C BOOLEAN
VmExitDispatcher(PGUEST_CONTEXT Context)
{
	return VmExit::VmExitDispatcher(Context);
}

VOID
VmExit::AdvanceGuestRip()
{
	ULONG64 GuestRip = 0;
	ULONG64 InstructionLength = 0;

	__vmx_vmread(Guest_State_Fields::Guest_RIP, &GuestRip);
	__vmx_vmread(Read_Only_Data_Fields::VM_Exit_Instruction_Length, &InstructionLength);

	__vmx_vmwrite(Guest_State_Fields::Guest_RIP, GuestRip + InstructionLength);
	return;
}

VOID
VmExit::HandleCpuid(PGUEST_CONTEXT Context)
{
	CPUID CPU_Information = { 0 };
	__cpuidex((PINT32)&CPU_Information, (INT32)Context->Rax, (INT32)Context->Rcx);

	Context->Rax = CPU_Information.eax;
	Context->Rbx = CPU_Information.ebx;
	if ((Context->Rax & 0xFFFFFFFF) == 1)
	{
		CPU_Information.ecx.ECX_01H.IsHypervisorEnable = FALSE;
		CPU_Information.ecx.ECX_01H.VMX = FALSE;
	}
	Context->Rcx = CPU_Information.ecx.info;
	Context->Rdx = CPU_Information.edx;
	return;
}

BOOLEAN
VmExit::HandleVmCall(PGUEST_CONTEXT Context)
{
	if (Context->Rcx == VMCALL_STOP_HV) VmCall::VmcallStopHv(Context);

	LogInformation("VMCALL received from guest with unknown code: 0x%llX", Context->Rcx);
	return FALSE;
}

BOOLEAN
VmExit::HandleRdmsr(PGUEST_CONTEXT Context)
{
	ULONG Msr = (ULONG)Context->Rcx;
	ULONG64 MsrValue = 0;
	BOOLEAN IsInjectGP = FALSE;

	switch (Msr)
	{
	case MSRs::IA32_VMX_BASIC:			case MSRs::IA32_VMX_PINBASED_CTLS:		case MSRs::IA32_VMX_PROCBASED_CTLS:
	case MSRs::IA32_VMX_EXIT_CTLS:		case MSRs::IA32_VMX_ENTRY_CTLS:			case MSRs::IA32_VMX_MISC:
	case MSRs::IA32_VMX_CR0_FIXED0:		case MSRs::IA32_VMX_CR0_FIXED1:			case MSRs::IA32_VMX_CR4_FIXED0:
	case MSRs::IA32_VMX_CR4_FIXED1:		case MSRs::IA32_VMX_VMCS_ENUM:			case MSRs::IA32_VMX_PROCBASED_CTLS2:
	case MSRs::IA32_VMX_EPT_VPID_CAP:	case MSRs::IA32_VMX_TRUE_PINBASED_CTLS: case MSRs::IA32_VMX_TRUE_PROCBASED_CTLS:
	case MSRs::IA32_VMX_TRUE_EXIT_CTLS:	case MSRs::IA32_VMX_TRUE_ENTRY_CTLS:	case MSRs::IA32_VMX_VMFUNC:
	case MSRs::IA32_VMX_PROCBASED_CTLS3:case MSRs::IA32_VMX_EXIT_CTLS2:
		InjectEvent::EventInjectGeneralProtection();
		IsInjectGP = TRUE;
		break;
	case MSRs::IA32_FEATURE_CONTROL:
		MsrValue = __readmsr(Msr);
		MsrValue &= ~Util::Bit(1); // Clear the EnableVmxonInSMX bit
		MsrValue &= ~Util::Bit(2); // Clear the EnableVmxonOutsideSMX bit
		break;
	case MSRs::IA32_FS_BASE:
		__vmx_vmread(Guest_State_Fields::Guest_FS_Base, &MsrValue);
		break;

	case MSRs::IA32_GS_BASE:
		__vmx_vmread(Guest_State_Fields::Guest_GS_Base, &MsrValue);
		break;

	default:
		MsrValue = __readmsr(Msr);
		break;
	}

	if (IsInjectGP)
	{
		return FALSE;
	}

	Context->Rax = MsrValue & 0xFFFFFFFF;
	Context->Rdx = (MsrValue >> 32) & 0xFFFFFFFF;
	return TRUE;
}

BOOLEAN
VmExit::HandleWrmsr(PGUEST_CONTEXT Context)
{
	ULONG Msr = (ULONG)Context->Rcx;
	ULONG64 Value = ((Context->Rdx & 0xFFFFFFFF) << 32) | (Context->Rax & 0xFFFFFFFF);
	BOOLEAN IsInjectGP = FALSE;

	switch (Msr)
	{
	case MSRs::IA32_VMX_BASIC:			case MSRs::IA32_VMX_PINBASED_CTLS:		case MSRs::IA32_VMX_PROCBASED_CTLS:
	case MSRs::IA32_VMX_EXIT_CTLS:		case MSRs::IA32_VMX_ENTRY_CTLS:			case MSRs::IA32_VMX_MISC:
	case MSRs::IA32_VMX_CR0_FIXED0:		case MSRs::IA32_VMX_CR0_FIXED1:			case MSRs::IA32_VMX_CR4_FIXED0:
	case MSRs::IA32_VMX_CR4_FIXED1:		case MSRs::IA32_VMX_VMCS_ENUM:			case MSRs::IA32_VMX_PROCBASED_CTLS2:
	case MSRs::IA32_VMX_EPT_VPID_CAP:	case MSRs::IA32_VMX_TRUE_PINBASED_CTLS: case MSRs::IA32_VMX_TRUE_PROCBASED_CTLS:
	case MSRs::IA32_VMX_TRUE_EXIT_CTLS:	case MSRs::IA32_VMX_TRUE_ENTRY_CTLS:	case MSRs::IA32_VMX_VMFUNC:
	case MSRs::IA32_VMX_PROCBASED_CTLS3:case MSRs::IA32_VMX_EXIT_CTLS2:			case MSRs::IA32_FEATURE_CONTROL:
		InjectEvent::EventInjectGeneralProtection();
		IsInjectGP = TRUE;
		break;
	case MSRs::IA32_LSTAR:
		__writemsr(Msr, Value);
		break;
	case MSRs::IA32_FS_BASE:
		__vmx_vmwrite(Guest_State_Fields::Guest_FS_Base, Value);
		break;

	case MSRs::IA32_GS_BASE:
		__vmx_vmwrite(Guest_State_Fields::Guest_GS_Base, Value);
		break;
	default:
		__writemsr(Msr, Value);
		break;
	}

	if (IsInjectGP)
	{
		return FALSE;
	}

	return TRUE;
}
