#pragma once
#include <ntifs.h>
#include "SupportVmx.h"
#include "Vcpu/VcpuState.h"

#define VMXON_SIZE 0x1000
#define VMCS_SIZE 0x1000
#define MSR_BITMAP_SIZE 0x1000
#define VMM_STACK_SIZE 0x10000

EXTERN_C ULONG_PTR AsmVmxContextSaveState(ULONG_PTR Argument);
EXTERN_C ULONG_PTR AsmVmxContextRestoreState(ULONG_PTR Argument);

//Manage Hyper ON/OFF, VMCS, INVEPT/INVVPID
class Vmx
{
public:
	static CPUID cpuid;

	static BOOLEAN IsVmxSupported();
	static VOID PrepareVmxOperation();
	static BOOLEAN VmxInitializeVmxonRegion(PVCPU_STATE CurrentCPU);
	static BOOLEAN VmxInitializeVmcsRegion(PVCPU_STATE CurrentCPU);
	static BOOLEAN VmxInitializeMsrBitmap(PVCPU_STATE CurrentCPU);
	static BOOLEAN VmxInitializeVmmStack(PVCPU_STATE CurrentCPU);
	static BOOLEAN VmxEnterRootMode(PVCPU_STATE CurrentCPU);
};