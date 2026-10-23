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

typedef VOID (*fnPreHookCallback)(PGUEST_CONTEXT Context);
typedef BOOLEAN(*fnPreHookWithResult)(PGUEST_CONTEXT Context);
typedef BOOLEAN (*fnPreHookFilterCallback)(PGUEST_CONTEXT Context);
typedef ULONG64 (*fnPostHookCallback)(PGUEST_CONTEXT Context, ULONG64 Result);

typedef struct _EPT_POST_HOOK_ENTRY
{
	LIST_ENTRY ListEntry;

	PVOID TargetFunction;
	PVOID HookFunction;        // fnPostHookCallback
	PVOID PreHookFunction;     // Optional Pre-Hook handler
	PVOID OriginalTrampoline;  // Original function trampoline
	PVOID EntryThunk;          // Dynamic 24-byte executable thunk
	BOOLEAN UsePreHookResult;  // If TRUE, evaluates PreHook return value to determine whether to skip target function
	BOOLEAN IsHooked;
} EPT_POST_HOOK_ENTRY, * PEPT_POST_HOOK_ENTRY;
typedef EPT_POST_HOOK_ENTRY EPT_UTIL_HOOK_ENTRY;
typedef PEPT_POST_HOOK_ENTRY PEPT_UTIL_HOOK_ENTRY;

#define POST_HOOK_DEFAULT_BUCKETS 256
#define POST_HOOK_MIN_BUCKETS     64
#define POST_HOOK_MAX_BUCKETS     65536
#define POST_HOOK_INITIAL_FRAMES  256
#define POST_HOOK_CHUNK_FRAMES    128

// Backward compatibility defines
#define POST_HOOK_HASH_BUCKETS    POST_HOOK_DEFAULT_BUCKETS
#define POST_HOOK_HASH_MASK       (POST_HOOK_DEFAULT_BUCKETS - 1)

typedef struct alignas(16) _POST_HOOK_CALL_FRAME
{
	SLIST_ENTRY SListEntry;              // For lock-free free list (cmpxchg16b requires 16-byte alignment)
	struct _POST_HOOK_CALL_FRAME* Next;  // Hash bucket collision chain
	volatile PVOID Thread;
	volatile ULONG64 CallerRsp;
	ULONG64 OriginalReturnRip;
	PVOID* ReturnRipSlot;               // Pointer to stack slot containing return address
	PEPT_POST_HOOK_ENTRY Entry;
	ULONG64 Rcx;
	ULONG64 Rdx;
	ULONG64 R8;
	ULONG64 R9;
} POST_HOOK_CALL_FRAME, * PPOST_HOOK_CALL_FRAME;
typedef POST_HOOK_CALL_FRAME UTIL_HOOK_CALL_FRAME;
typedef PPOST_HOOK_CALL_FRAME PUTIL_HOOK_CALL_FRAME;

typedef struct alignas(16) _POST_HOOK_CHUNK
{
	SLIST_ENTRY SListEntry;
	ULONG FrameCount;
	ULONG Reserved;
	PPOST_HOOK_CALL_FRAME Frames;
} POST_HOOK_CHUNK, * PPOST_HOOK_CHUNK;
typedef POST_HOOK_CHUNK UTIL_HOOK_CHUNK;
typedef PPOST_HOOK_CHUNK PUTIL_HOOK_CHUNK;

typedef struct alignas(64) _POST_HOOK_BUCKET
{
	volatile LONG Lock;
	PPOST_HOOK_CALL_FRAME Head;
} POST_HOOK_BUCKET, * PPOST_HOOK_BUCKET;
typedef POST_HOOK_BUCKET UTIL_HOOK_BUCKET;
typedef PPOST_HOOK_BUCKET PUTIL_HOOK_BUCKET;

inline ULONG PostHookHash(PVOID Thread);

EXTERN_C VOID AsmPostHookCommonEntry(VOID);
EXTERN_C VOID AsmPostHookReturnStub(VOID);
EXTERN_C VOID AsmCommonRetStub(VOID);

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
	static SLIST_HEADER ChunkListHead;

	static PPOST_HOOK_BUCKET PostHookBuckets;
	static ULONG PostHookBucketCount;
	static ULONG PostHookBucketMask;

	static BOOLEAN Initialize(ULONG BucketCount = POST_HOOK_DEFAULT_BUCKETS);
	static VOID Destroy();

	static BOOLEAN AllocateMoreFrames(ULONG FrameCount = POST_HOOK_CHUNK_FRAMES);

	static NTSTATUS Hook(PVOID TargetFunction, PVOID HookHandler, PVOID* OriginalFunction = NULL);
	static NTSTATUS Unhook(PVOID TargetFunction);
	static VOID UnhookAll();

	/*
	Internal Post-Hook mechanism : traps target function entry via stealth EPT,
	rewrites stack return address to AsmPostHookReturnStub.

	@param PVOID TargetFunction						 -> The target function to hook
	@param VOID PreHookFunction(Context)			 -> Executed before enter function
	@param ULONG64 PostHookFunction(Context, Result) -> Executed after exit function
	@param PVOID* OriginalFunction					 -> Cleanup and handover to target function
	@param BOOLEAN UsePreHookResult					 -> Enables using hook result to determine whether to bypass the target function

	@return Is hook successful?
	*/
	static BOOLEAN PostHook(PVOID TargetFunction, fnPreHookCallback PreHookFunction = NULL, fnPostHookCallback PostHookFunction = NULL, PVOID* OriginalFunction = NULL, BOOLEAN UsePreHookResult = FALSE);
	static BOOLEAN PostHook(PVOID TargetFunction, fnPreHookFilterCallback PreHookFilterFunction, fnPostHookCallback PostHookFunction = NULL, PVOID* OriginalFunction = NULL);
	static BOOLEAN PostHook(PVOID TargetFunction, fnPostHookCallback PostHookFunction);

	static BOOLEAN UnhookPostHook(PVOID TargetFunction);
	static VOID RevertPostHookFrames(PEPT_POST_HOOK_ENTRY TargetEntry = NULL);
	static PEPT_POST_HOOK_ENTRY FindPostHookEntryByAddress(PVOID TargetAddress);

	static BOOLEAN UtilHook(PVOID TargetFunction, fnPreHookCallback PreHookFunction = NULL, fnPostHookCallback PostHookFunction = NULL, PVOID* OriginalFunction = NULL, BOOLEAN UsePreHookResult = FALSE);
	static BOOLEAN UtilHook(PVOID TargetFunction, fnPreHookFilterCallback PreHookFilterFunction, fnPostHookCallback PostHookFunction = NULL, PVOID* OriginalFunction = NULL);
	static BOOLEAN UtilHook(PVOID TargetFunction, fnPostHookCallback PostHookFunction);

	#define EPT_ENTRY_HOOK(TargetFunction, PreHookFunction) EptHook::UtilHook((PVOID)(TargetFunction), (fnPreHookCallback)(PreHookFunction), (fnPostHookCallback)NULL, (PVOID*)NULL, FALSE)
	#define EPT_RETURN_HOOK(TargetFunction, PostHookFunction) EptHook::UtilHook((PVOID)(TargetFunction), (fnPostHookCallback)(PostHookFunction))
	#define EPT_INTERCEPT_HOOK(TargetFunction, PreHookFunction, PostHookFunction) EptHook::UtilHook((PVOID)(TargetFunction), (fnPreHookCallback)(PreHookFunction), (fnPostHookCallback)(PostHookFunction), (PVOID*)NULL, FALSE)
	#define EPT_FILTER_HOOK(TargetFunction, PreHookFunction) EptHook::UtilHook((PVOID)(TargetFunction), (fnPreHookFilterCallback)(PreHookFunction), (fnPostHookCallback)NULL, (PVOID*)NULL)
	#define EPT_FILTER_INTERCEPT_HOOK(TargetFunction, PreHookFunction, PostHookFunction) EptHook::UtilHook((PVOID)(TargetFunction), (fnPreHookFilterCallback)(PreHookFunction), (fnPostHookCallback)(PostHookFunction), (PVOID*)NULL)

	#define EPT_ENTRY_HOOK_EX(TargetFunction, PreHookFunction, OriginalFunction) EptHook::UtilHook((PVOID)(TargetFunction), (fnPreHookCallback)(PreHookFunction), (fnPostHookCallback)NULL, (PVOID*)(OriginalFunction), FALSE)
	#define EPT_FILTER_HOOK_EX(TargetFunction, PreHookFunction, OriginalFunction) EptHook::UtilHook((PVOID)(TargetFunction), (fnPreHookFilterCallback)(PreHookFunction), (fnPostHookCallback)NULL, (PVOID*)(OriginalFunction))

	static BOOLEAN HandleEptViolation(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress, EPT_VIOLATION_QUALIFICATION Qualification);
	static BOOLEAN HandleMonitorTrapFlag(PGUEST_CONTEXT Context);

	static PEPT_HOOK_ENTRY FindHookEntryByPfn(ULONG64 Pfn);
	static PEPT_HOOK_ENTRY FindHookEntryByAddress(PVOID TargetAddress);

	static volatile LONG alignas(64) EptViolationLock;
};

inline ULONG PostHookHash(PVOID Thread)
{
	ULONG_PTR Key = (ULONG_PTR)Thread >> 4;
	return (ULONG)(((Key * 0x9E3779B97F4A7C15ULL) >> 32) & EptHook::PostHookBucketMask);
}
