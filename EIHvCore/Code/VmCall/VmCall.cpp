#include "VmCall/VmCall.h"
#include "Vcpu/Vcpu.h"
#include "Vmcs/Vmcs.h"
#include "Vmcs/VmcsField.h"
#include "Msr/MsrControlField.h"
#include "Util.h"
#include "Log/Log.h"
#include <intrin.h>


VOID 
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
	ULONG64 Guest_Kernel_GS_Base = 0;

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
	Guest_Kernel_GS_Base = __readmsr(MSRs::IA32_KERNEL_GS_BASE);

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

	// Restore GDTR and IDTR (Crucial for PatchGuard: VM-Exit sets GDTR.Limit and IDTR.Limit to 0xFFFF)
	PSEUDO_DESCRIPTOR_64 Gdtr = { 0 };
	Gdtr.Base = GuestGdtrBase;
	Gdtr.Limit = (USHORT)GuestGdtrLimit;
	AsmSetGdt(&Gdtr);

	PSEUDO_DESCRIPTOR_64 Idtr = { 0 };
	Idtr.Base = GuestIdtrBase;
	Idtr.Limit = (USHORT)GuestIdtrLimit;
	AsmSetIdt(&Idtr);

	AsmSetDs((USHORT)GuestDs);
	AsmSetEs((USHORT)GuestEs);
	if (GuestLdtr != 0) AsmSetLdtr((USHORT)GuestLdtr);

	__writemsr(MSRs::IA32_FS_BASE, GuestFsBase);
	__writemsr(MSRs::IA32_GS_BASE, GuestGsBase);
	__writemsr(MSRs::IA32_KERNEL_GS_BASE, Guest_Kernel_GS_Base);

	__writedr(7, GuestDr7);

	//__writecr3(GuestCr3);

	//__writecr0(GuestCr0);

	//__vmx_off();

	//// ensure CR4.VMXE bit 13 is cleared
	//__writecr4(GuestCr4 & ~Util::Bit(13));


	//PULONG64 IRETQ_FRAME = (PULONG64)_AddressOfReturnAddress();
	//ULONG64 IRETQ_FRAME[6];

	//// replacement shadow space for iretq
	//IRETQ_FRAME[0] = Context->Rax;
	//IRETQ_FRAME[1] = Context->GuestRip;
	//IRETQ_FRAME[2] = Context->GuestCs;
	//IRETQ_FRAME[3] = Context->GuestRflags;
	//IRETQ_FRAME[4] = Context->GuestRsp;
	//IRETQ_FRAME[5] = Context->GuestSs;

	if (Vcpu::VcpuState)
	{
		Vcpu::VcpuState[CurrentProcessorNumber].VmxonStatus = FALSE;
		Vcpu::VcpuState[CurrentProcessorNumber].VmcsLaunchStatus = FALSE;
	}

	AsmDisableVmxOperation(GuestCr0, GuestCr3, GuestCr4 & ~Util::Bit(13), Context);

	return;
}