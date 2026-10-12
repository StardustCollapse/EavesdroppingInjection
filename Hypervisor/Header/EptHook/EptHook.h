#pragma once
#include <ntifs.h>
#include "Ept/Ept.h"
#include "VmExit/VmExit.h"

#define EPT_HOOK_JMP_SIZE 12

// 12-byte absolute jump structure -> mov rax, TargetAddress; jmp rax
#pragma pack(push, 1)
typedef struct _MOV_RAX_JMP_RAX_12
{
	UCHAR MovOpcode[2];    // 48 B8
	ULONG64 TargetAddress; // 64-bit destination address
	UCHAR JmpOpcode[2];    // FF E0
} MOV_RAX_JMP_RAX_12, * PMOV_RAX_JMP_RAX_12;
#pragma pack(pop)

// 14-byte absolute indirect jump: jmp qword ptr [rip+0] (preserves all registers)
#pragma pack(push, 1)
typedef struct _ABSOLUTE_JUMP_14
{
	UCHAR Opcode[6];       // FF 25 00 00 00 00 (jmp qword ptr [rip+0])
	ULONG64 TargetAddress; // 64-bit destination address
} ABSOLUTE_JUMP_14, * PABSOLUTE_JUMP_14;
#pragma pack(pop)

typedef struct _EPT_HOOK_ENTRY
{
	LIST_ENTRY ListEntry;

	PVOID TargetVirtualAddress;
	ULONG64 TargetPhysicalAddress;
	ULONG64 OriginalPfn;
	ULONG64 FakePfn;

	PVOID FakePageVirtualAddress;
	PEPT_PTE TargetPte;

	PVOID HookHandler;
	PVOID OriginalTrampoline;
	ULONG TrampolineSize;

	PUCHAR OriginalBytes; // Backup of original bytes at TargetVirtualAddress
	ULONG OriginalBytesSize;
	ULONG PageOffset;

	BOOLEAN IsHooked;
} EPT_HOOK_ENTRY, * PEPT_HOOK_ENTRY;

typedef ULONG64 (*fnPostHookCallback)(PGUEST_CONTEXT Context, ULONG64 Result);

typedef struct _EPT_POST_HOOK_ENTRY
{
	LIST_ENTRY ListEntry;

	PVOID TargetFunction;
	PVOID HookFunction;        // fnPostHookCallback
	PVOID OriginalTrampoline;  // Original function trampoline
	PVOID EntryThunk;          // Dynamic 24-byte executable thunk
	BOOLEAN IsHooked;
} EPT_POST_HOOK_ENTRY, * PEPT_POST_HOOK_ENTRY;

#define POST_HOOK_HASH_BUCKETS 256
#define POST_HOOK_HASH_MASK    (POST_HOOK_HASH_BUCKETS - 1)
#define POST_HOOK_MAX_FRAMES   4096

#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
typedef struct alignas(16) _POST_HOOK_CALL_FRAME
{
	SLIST_ENTRY SListEntry;              // For lock-free free list (cmpxchg16b requires 16-byte alignment)
	struct _POST_HOOK_CALL_FRAME* Next;  // Hash bucket collision chain
	volatile PVOID Thread;
	volatile ULONG64 CallerRsp;
	ULONG64 OriginalReturnRip;
	PEPT_POST_HOOK_ENTRY Entry;
	ULONG64 Rcx;
	ULONG64 Rdx;
	ULONG64 R8;
	ULONG64 R9;
} POST_HOOK_CALL_FRAME, * PPOST_HOOK_CALL_FRAME;

typedef struct alignas(64) _POST_HOOK_BUCKET
{
	volatile LONG Lock;
	PPOST_HOOK_CALL_FRAME Head;
} POST_HOOK_BUCKET, * PPOST_HOOK_BUCKET;
#pragma warning(pop)

inline ULONG PostHookHash(PVOID Thread)
{
	ULONG_PTR Key = (ULONG_PTR)Thread >> 4;
	return (ULONG)((Key * 0x9E3779B97F4A7C15ULL) >> (64 - 8));
}

EXTERN_C VOID AsmPostHookCommonEntry(VOID);
EXTERN_C VOID AsmPostHookReturnStub(VOID);

EXTERN_C PVOID PostHookOnEntry(
	PEPT_POST_HOOK_ENTRY Entry,
	PVOID* pCallerReturnRipSlot,
	ULONG64 CallerRsp,
	PULONG64 pSavedArgs
);

EXTERN_C ULONG64 PostHookOnReturn(
	ULONG64 ReturnValue,
	ULONG64 CurrentRsp,
	PULONG64 pOutOriginalReturnRip,
	PVOID Reserved
);

class EptHook
{
public:
	static LIST_ENTRY HookList;
	static LIST_ENTRY PostHookList;
	static BOOLEAN Initialized;

	static SLIST_HEADER FreeFrameListHead;
	static PPOST_HOOK_CALL_FRAME PostHookFramePool;
	static POST_HOOK_BUCKET PostHookBuckets[POST_HOOK_HASH_BUCKETS];

	static BOOLEAN Initialize();
	static VOID Destroy();

	static NTSTATUS Hook(PVOID TargetFunction, PVOID HookHandler, PVOID* OriginalFunction = NULL);
	static NTSTATUS Unhook(PVOID TargetFunction);
	static VOID UnhookAll();

	// Traps target function entry via stealth EPT, rewrites stack return address to AsmPostHookReturnStub
	static BOOLEAN PostHook(PVOID TargetFunction, PVOID HookFunction);
	static BOOLEAN UnhookPostHook(PVOID TargetFunction);

	static BOOLEAN HandleEptViolation(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress, EPT_VIOLATION_QUALIFICATION Qualification);
	static BOOLEAN HandleMonitorTrapFlag(PGUEST_CONTEXT Context);

	static PEPT_HOOK_ENTRY FindHookEntryByPfn(ULONG64 Pfn);
	static PEPT_HOOK_ENTRY FindHookEntryByAddress(PVOID TargetAddress);

	static PEPT_POST_HOOK_ENTRY FindPostHookEntryByAddress(PVOID TargetAddress);

	static volatile LONG alignas(64) EptViolationLock;
};