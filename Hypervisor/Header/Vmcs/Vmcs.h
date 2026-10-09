#pragma once
#include <ntifs.h>
#include <intrin.h>
#include "VmcsField.h"
#include "SegmentRegister.h"
#include "Vcpu/Vcpu.h"
#include "VmExit/VmExit.h"

EXTERN_C USHORT AsmGetCs(void);
EXTERN_C USHORT AsmGetDs(void);
EXTERN_C USHORT AsmGetEs(void);
EXTERN_C USHORT AsmGetSs(void);
EXTERN_C USHORT AsmGetFs(void);
EXTERN_C USHORT AsmGetGs(void);
EXTERN_C USHORT AsmGetLdtr(void);
EXTERN_C USHORT AsmGetTr(void);
EXTERN_C ULONG64 AsmGetGdtBase(void);
EXTERN_C ULONG64 AsmGetIdtBase(void);
EXTERN_C USHORT AsmGetGdtLimit(void);
EXTERN_C USHORT AsmGetIdtLimit(void);
EXTERN_C ULONG64 AsmGetRflags(void);
EXTERN_C VOID AsmSetGdt(PPSEUDO_DESCRIPTOR_64 GdtDescriptor);
EXTERN_C VOID AsmSetIdt(PPSEUDO_DESCRIPTOR_64 IdtDescriptor);
EXTERN_C VOID AsmSetDs(USHORT Selector);
EXTERN_C VOID AsmSetEs(USHORT Selector);
EXTERN_C VOID AsmSetLdtr(USHORT Selector);

//Fill Vmcs Fields
class Vmcs
{
public:
	static BOOLEAN VmxClearVmcs(PVCPU_STATE CurrentCPU);
	static BOOLEAN VmxLoadVmcs(PVCPU_STATE CurrentCPU);
	static BOOLEAN VmxSetupVmcs(PVCPU_STATE CurrentCPU, PVOID GuestStack);

	static BOOLEAN VmxSetupGuestState(PVOID GuestStack);
	static BOOLEAN VmxSetupHostState(PVCPU_STATE CurrentCPU);
	static BOOLEAN VmxSetupControls(PVCPU_STATE CurrentCPU);

	static BOOLEAN AdjustControl(ULONG DesiredValue, ULONG MsrIndex, PULONG ActualValue);
	static ULONG64 GetSegmentBase(ULONG64 GdtBase, USHORT Selector);
	static BOOLEAN LoadGuestSegment(ULONG64 GdtBase, USHORT Selector, UCHAR SegmentRegister);
};