#pragma once
#include <ntifs.h>

typedef struct _GUEST_CONTEXT
{
	ULONG64 R15;
	ULONG64 R14;
	ULONG64 R13;
	ULONG64 R12;
	ULONG64 R11;
	ULONG64 R10;
	ULONG64 R9;
	ULONG64 R8;
	ULONG64 Rdi;
	ULONG64 Rsi;
	ULONG64 Rbp;
	ULONG64 Rbx;
	ULONG64 Rdx;
	ULONG64 Rcx;
	ULONG64 Rax;
	ULONG64 GuestRip;
	ULONG64 GuestCs;
	ULONG64 GuestRflags;
	ULONG64 GuestRsp;
	ULONG64 GuestSs;
} GUEST_CONTEXT, * PGUEST_CONTEXT;

EXTERN_C VOID AsmVmExitHandler(VOID);
EXTERN_C BOOLEAN VmExitDispatcher(PGUEST_CONTEXT Context);
EXTERN_C VOID AsmDisableVmxOperationVmcall(VOID);

//Manage VmExit Handler
class VmExit
{
public:
	static BOOLEAN VmExitDispatcher(PGUEST_CONTEXT Context);
	static VOID HandleCpuid(PGUEST_CONTEXT Context);
	static BOOLEAN HandleVmCall(PGUEST_CONTEXT Context);
	static BOOLEAN HandleRdmsr(PGUEST_CONTEXT Context);
	static BOOLEAN HandleWrmsr(PGUEST_CONTEXT Context);
	static VOID AdvanceGuestRip();
};