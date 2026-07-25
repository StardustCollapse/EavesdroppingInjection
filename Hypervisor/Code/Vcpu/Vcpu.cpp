#include "Vcpu/Vcpu.h"
#include "Vmx/Vmx.h"
#include "Log/Log.h"

PVCPU_STATE Vcpu::VcpuState = NULL;

//_Use_decl_annotations_
VOID
Vcpu::InitializeVcpuState(PVOID GuestStack)
{
	UNREFERENCED_PARAMETER(GuestStack);
	ULONG CurrentProcessorNumber = KeGetCurrentProcessorNumberEx(NULL);
	PVCPU_STATE CurrentVcpuState = &Vcpu::VcpuState[CurrentProcessorNumber];

	Vmx::PrepareVmxOperation();

	Vmx::VmxEnterRootMode(CurrentVcpuState);

	//InComplete



	return;
}

EXTERN_C VOID
InitializeVcpuState(PVOID GuestStack)
{
	Vcpu::InitializeVcpuState(GuestStack);
	return;
}