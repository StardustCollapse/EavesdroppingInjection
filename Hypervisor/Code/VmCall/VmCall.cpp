#include "VmCall/VmCall.h"
#include "Vcpu/Vcpu.h"
#include "Vmcs/VmcsField.h"
#include "Util.h"
#include "Log/Log.h"
#include <intrin.h>


BOOLEAN 
VmCall::VmcallStopHv(PGUEST_CONTEXT Context)
{
	ULONG64 GuestRip = NULL;
	ULONG64 GuestRsp = NULL;
	ULONG64 GuestRflags = NULL;
	ULONG64 GuestCs = NULL;
	ULONG64 GuestSs = NULL;
	ULONG64 InstructionLength = NULL;
	ULONG64 GuestCr3 = NULL;

	ULONG CurrentProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
	LogInformation("Core %u received VMCALL_STOP_HV. Stopping hypervisor...", CurrentProcessorNumber);

	__vmx_vmread(Guest_State_Fields::Guest_RIP, &GuestRip);
	__vmx_vmread(Guest_State_Fields::Guest_RSP, &GuestRsp);
	__vmx_vmread(Guest_State_Fields::Guest_RFLAGS, &GuestRflags);
	__vmx_vmread(Guest_State_Fields::Guest_CS_Selector, &GuestCs);
	__vmx_vmread(Guest_State_Fields::Guest_SS_Selector, &GuestSs);
	__vmx_vmread(Read_Only_Data_Fields::VM_Exit_Instruction_Length, &InstructionLength);
	__vmx_vmread(Guest_State_Fields::Guest_CR3, &GuestCr3);

	if (GuestCs == 0) GuestCs = 0x10;
	if (GuestSs == 0) GuestSs = 0x18;

	Context->GuestRip = GuestRip + InstructionLength;
	Context->GuestCs = GuestCs;
	Context->GuestRflags = GuestRflags;
	Context->GuestRsp = GuestRsp;
	Context->GuestSs = GuestSs;
	Context->Rax = 0;

	// Restore original guest CR3 before turning off VMX
	__writecr3(GuestCr3);

	__vmx_off();

	// Disable CR4.VMXE (bit 13)
	__writecr4(__readcr4() & ~Util::Bit(13));

	if (Vcpu::VcpuState)
	{
		Vcpu::VcpuState[CurrentProcessorNumber].VmxonStatus = FALSE;
		Vcpu::VcpuState[CurrentProcessorNumber].VmcsLaunchStatus = FALSE;
	}

	return TRUE;
}