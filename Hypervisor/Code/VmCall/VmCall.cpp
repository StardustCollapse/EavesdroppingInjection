#include "VmCall/VmCall.h"
#include "Vcpu/Vcpu.h"
#include "Vmcs/Vmcs.h"
#include "Vmcs/VmcsField.h"
#include "Msr/MsrControlField.h"
#include "Util.h"
#include "Log/Log.h"
#include <intrin.h>


BOOLEAN 
VmCall::VmcallStopHv(PGUEST_CONTEXT Context)
{
	ULONG64 GuestRip = 0;
	ULONG64 GuestRsp = 0;
	ULONG64 GuestRflags = 0;
	ULONG64 GuestCs = 0;
	ULONG64 GuestSs = 0;
	ULONG64 InstructionLength = 0;

	ULONG64 GuestCr0 = 0;
	ULONG64 GuestCr3 = 0;
	ULONG64 GuestCr4 = 0;
	ULONG64 GuestDr7 = 0;

	ULONG64 GuestDs = 0;
	ULONG64 GuestEs = 0;
	ULONG64 GuestLdtr = 0;

	ULONG64 GuestFsBase = 0;
	ULONG64 GuestGsBase = 0;

	ULONG64 GuestGdtrBase = 0;
	ULONG64 GuestGdtrLimit = 0;
	ULONG64 GuestIdtrBase = 0;
	ULONG64 GuestIdtrLimit = 0;

	ULONG CurrentProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
	LogInformation("Core %u received VMCALL_STOP_HV. Stopping hypervisor...", CurrentProcessorNumber);

	__vmx_vmread(Guest_State_Fields::Guest_RIP, &GuestRip);
	__vmx_vmread(Guest_State_Fields::Guest_RSP, &GuestRsp);
	__vmx_vmread(Guest_State_Fields::Guest_RFLAGS, &GuestRflags);
	__vmx_vmread(Guest_State_Fields::Guest_CS_Selector, &GuestCs);
	__vmx_vmread(Guest_State_Fields::Guest_SS_Selector, &GuestSs);
	__vmx_vmread(Read_Only_Data_Fields::VM_Exit_Instruction_Length, &InstructionLength);

	__vmx_vmread(Guest_State_Fields::Guest_CR0, &GuestCr0);
	__vmx_vmread(Guest_State_Fields::Guest_CR3, &GuestCr3);
	__vmx_vmread(Guest_State_Fields::Guest_CR4, &GuestCr4);
	__vmx_vmread(Guest_State_Fields::Guest_DR7, &GuestDr7);

	__vmx_vmread(Guest_State_Fields::Guest_FS_Base, &GuestFsBase);
	__vmx_vmread(Guest_State_Fields::Guest_GS_Base, &GuestGsBase);

	__vmx_vmread(Guest_State_Fields::Guest_GDTR_Base, &GuestGdtrBase);
	__vmx_vmread(Guest_State_Fields::Guest_GDTR_Limit, &GuestGdtrLimit);
	__vmx_vmread(Guest_State_Fields::Guest_IDTR_Base, &GuestIdtrBase);
	__vmx_vmread(Guest_State_Fields::Guest_IDTR_Limit, &GuestIdtrLimit);

	__vmx_vmread(Guest_State_Fields::Guest_DS_Selector, &GuestDs);
	__vmx_vmread(Guest_State_Fields::Guest_ES_Selector, &GuestEs);
	__vmx_vmread(Guest_State_Fields::Guest_LDTR_Selector, &GuestLdtr);

	if (GuestCs == 0) GuestCs = 0x10;
	if (GuestSs == 0) GuestSs = 0x18;

	Context->GuestRip = GuestRip + InstructionLength;
	Context->GuestCs = GuestCs;
	Context->GuestRflags = GuestRflags;
	Context->GuestRsp = GuestRsp;
	Context->GuestSs = GuestSs;
	Context->Rax = STATUS_SUCCESS;

	// 1. Restore GDTR and IDTR (Crucial for PatchGuard: VM-Exit sets GDTR.Limit and IDTR.Limit to 0xFFFF)
	PSEUDO_DESCRIPTOR Gdtr = { 0 };
	Gdtr.Base = GuestGdtrBase;
	Gdtr.Limit = (USHORT)GuestGdtrLimit;
	AsmSetGdt(&Gdtr);

	PSEUDO_DESCRIPTOR Idtr = { 0 };
	Idtr.Base = GuestIdtrBase;
	Idtr.Limit = (USHORT)GuestIdtrLimit;
	AsmSetIdt(&Idtr);

	// 2. Restore Segment Selectors (DS, ES, and LDTR if present)
	AsmSetDs((USHORT)GuestDs);
	AsmSetEs((USHORT)GuestEs);
	if (GuestLdtr != 0) AsmSetLdtr((USHORT)GuestLdtr);

	// 3. Restore FS and GS base MSRs
	__writemsr((ULONG)MSRs::IA32_FS_BASE, GuestFsBase);
	__writemsr((ULONG)MSRs::IA32_GS_BASE, GuestGsBase);

	// 3. Restore DR7
	__writedr(7, GuestDr7);

	// 4. Restore original guest CR3
	__writecr3(GuestCr3);

	// 5. Restore Guest CR0
	__writecr0(GuestCr0);

	// 6. Disable VMX operation
	__vmx_off();

	// 7. Restore Guest CR4 (ensuring CR4.VMXE bit 13 is cleared)
	__writecr4(GuestCr4 & ~Util::Bit(13));

	if (Vcpu::VcpuState)
	{
		Vcpu::VcpuState[CurrentProcessorNumber].VmxonStatus = FALSE;
		Vcpu::VcpuState[CurrentProcessorNumber].VmcsLaunchStatus = FALSE;
	}

	return TRUE;
}