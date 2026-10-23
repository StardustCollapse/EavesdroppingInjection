#include "EptHook/EptHook.h"
#include "Memory/PhysicalMemory.h"
#include "Memory/VirtualMemory.h"
#include "Vcpu/Vcpu.h"
#include "Vmcs/Vmcs.h"
#include "Msr/MsrControlField.h"
#include "Util.h"
#include "Log/Log.h"
#include "EptHook/HookGuard.h"
#include "HDE/hde64.h"

LIST_ENTRY EptHook::HookList = { 0 };
LIST_ENTRY EptHook::PostHookList = { 0 };
BOOLEAN EptHook::Initialized = FALSE;
SLIST_HEADER EptHook::ChunkListHead = { 0 };
SLIST_HEADER EptHook::FreeFrameListHead = { 0 };
PPOST_HOOK_BUCKET EptHook::PostHookBuckets = NULL;
ULONG EptHook::PostHookBucketCount = 0;
ULONG EptHook::PostHookBucketMask = 0;
volatile LONG alignas(64) EptHook::EptViolationLock = 0;

#pragma region Initialize
BOOLEAN
EptHook::Initialize(ULONG BucketCount)
{
	if (EptHook::Initialized)
	{
		return TRUE;
	}

	InitializeListHead(&EptHook::HookList);
	InitializeListHead(&EptHook::PostHookList);

	InitializeSListHead(&EptHook::ChunkListHead);
	InitializeSListHead(&EptHook::FreeFrameListHead);

	if (BucketCount < POST_HOOK_MIN_BUCKETS) BucketCount = POST_HOOK_MIN_BUCKETS;
	else if (BucketCount > POST_HOOK_MAX_BUCKETS) BucketCount = POST_HOOK_MAX_BUCKETS;

	ULONG Power = 1;
	while (Power < BucketCount) Power <<= 1;
	BucketCount = Power;

	// Dynamically allocate Hash Buckets
	SIZE_T TotalSize = sizeof(POST_HOOK_BUCKET) * BucketCount;
	PVOID Buffer = NULL;
	if (!VirtualMemory::AllocateNonPagedPool(&Buffer, TotalSize, FALSE))
	{
		LogError("Initialize: Failed to allocate PostHookBuckets pool (%llu bytes)", (ULONG64)TotalSize);
		return FALSE;
	}
	RtlZeroMemory(Buffer, TotalSize);

	EptHook::PostHookBuckets = (PPOST_HOOK_BUCKET)Buffer;
	EptHook::PostHookBucketCount = BucketCount;
	EptHook::PostHookBucketMask = BucketCount - 1;

	for (ULONG i = 0; i < BucketCount; i++)
	{
		EptHook::PostHookBuckets[i].Lock = 0;
		EptHook::PostHookBuckets[i].Head = NULL;
	}

	// Allocate initial dynamic call frame chunk
	if (!EptHook::AllocateMoreFrames(POST_HOOK_INITIAL_FRAMES))
	{
		LogError("Initialize: Failed to allocate initial PostHook frames");
		VirtualMemory::FreeNonPagedPool(Buffer);
		EptHook::PostHookBuckets = NULL;
		EptHook::PostHookBucketCount = 0;
		EptHook::PostHookBucketMask = 0;
		return FALSE;
	}

	EptHook::EptViolationLock = 0;
	EptHook::Initialized = TRUE;

	LogInformation("EPT Hook initialized successfully (Post-Hook Hash Table: %d buckets [%llu KB dynamic], Dynamic Frames: %d initial)",
		BucketCount, (ULONG64)(TotalSize / 1024), POST_HOOK_INITIAL_FRAMES);
	return TRUE;
}

BOOLEAN
EptHook::AllocateMoreFrames(ULONG FrameCount)
{
	SIZE_T TotalSize = sizeof(POST_HOOK_CHUNK) + sizeof(POST_HOOK_CALL_FRAME) * FrameCount;
	PVOID ChunkBuffer = NULL;
	if (!VirtualMemory::AllocateNonPagedPool(&ChunkBuffer, TotalSize, FALSE))
	{
		LogError("AllocateMoreFrames: Failed to allocate non-paged pool (%llu bytes)", (ULONG64)TotalSize);
		return FALSE;
	}
	RtlZeroMemory(ChunkBuffer, TotalSize);

	PPOST_HOOK_CHUNK Chunk = (PPOST_HOOK_CHUNK)ChunkBuffer;
	Chunk->FrameCount = FrameCount;
	Chunk->Frames = (PPOST_HOOK_CALL_FRAME)((PUCHAR)ChunkBuffer + sizeof(POST_HOOK_CHUNK));

	InterlockedPushEntrySList(&EptHook::ChunkListHead, &Chunk->SListEntry);

	PPOST_HOOK_CALL_FRAME Frames = Chunk->Frames;
	for (ULONG i = 0; i < FrameCount; i++)
	{
		InterlockedPushEntrySList(&EptHook::FreeFrameListHead, &Frames[i].SListEntry);
	}

	return TRUE;
}
#pragma endregion

#pragma region Destroy
VOID
EptHook::Destroy()
{
	if (!EptHook::Initialized)
	{
		return;
	}

	EptHook::UnhookAll();

	// Free all dynamically allocated frame chunks
	PSLIST_ENTRY ChunkEntry = NULL;
	while ((ChunkEntry = InterlockedPopEntrySList(&EptHook::ChunkListHead)) != NULL)
	{
		PPOST_HOOK_CHUNK Chunk = CONTAINING_RECORD(ChunkEntry, POST_HOOK_CHUNK, SListEntry);
		VirtualMemory::FreeNonPagedPool(Chunk);
	}

	// Free dynamically allocated hash buckets
	if (EptHook::PostHookBuckets != NULL)
	{
		VirtualMemory::FreeNonPagedPool(EptHook::PostHookBuckets);
		EptHook::PostHookBuckets = NULL;
	}
	EptHook::PostHookBucketCount = 0;
	EptHook::PostHookBucketMask = 0;

	EptHook::Initialized = FALSE;
	LogInformation("EPT Hook destroyed");
	return;
}

NTSTATUS
EptHook::Unhook(PVOID TargetFunction)
{
	if (!EptHook::Initialized || !TargetFunction)
	{
		return STATUS_INVALID_PARAMETER;
	}

	PEPT_HOOK_ENTRY Entry = EptHook::FindHookEntryByAddress(TargetFunction);
	if (Entry == NULL)
	{
		return STATUS_NOT_FOUND;
	}

	RemoveEntryList(&Entry->ListEntry);

	// Check if any other hook in the list still shares the same OriginalPfn
	BOOLEAN PageStillShared = (EptHook::FindHookEntryByPfn(Entry->OriginalPfn) != NULL);

	// Restore original page
	if (!PageStillShared)
	{
		Entry->TargetPte->Fields.PageFrameNumber = Entry->OriginalPfn;
		Entry->TargetPte->Fields.Read = 1;
		Entry->TargetPte->Fields.Write = 1;
		Entry->TargetPte->Fields.Execute = 1;

		if (Vcpu::VcpuState && Vcpu::VcpuState[0].VmcsLaunchStatus)
		{
			KeIpiGenericCall(Ept::InveptSingleContext, 0);
		}

		if (Entry->FakePageVirtualAddress != NULL)
		{
			PhysicalMemory::FreeContiguousMemory(Entry->FakePageVirtualAddress);
			Entry->FakePageVirtualAddress = NULL;
		}
	}
	else
	{
		RtlCopyMemory((PUCHAR)Entry->FakePageVirtualAddress + Entry->PageOffset, Entry->OriginalBytes, Entry->OriginalBytesSize);
	}

	if (Entry->OriginalTrampoline != NULL)
	{
		VirtualMemory::FreeNonPagedPool(Entry->OriginalTrampoline);
		Entry->OriginalTrampoline = NULL;
	}

	LogInformation("EPT Hook removed from %p", TargetFunction);
	VirtualMemory::FreeNonPagedPool(Entry->OriginalBytes);
	VirtualMemory::FreeNonPagedPool(Entry);
	return STATUS_SUCCESS;
}

// To call this function safely, ensure a sufficient delay has passed
BOOLEAN
EptHook::UnhookPostHook(PVOID TargetFunction)
{
	if (!EptHook::Initialized || !TargetFunction)
	{
		return FALSE;
	}

	PEPT_POST_HOOK_ENTRY Entry = EptHook::FindPostHookEntryByAddress(TargetFunction);
	if (Entry == NULL)
	{
		return FALSE;
	}

	RemoveEntryList(&Entry->ListEntry);

	// Remove underlying EPT Hook
	EptHook::Unhook(TargetFunction);

	// Revert stack return addresses on the spot for in-flight/blocking threads
	EptHook::RevertPostHookFrames(Entry);

	if (Entry->EntryThunk != NULL)
	{
		VirtualMemory::FreeNonPagedPool(Entry->EntryThunk);
		Entry->EntryThunk = NULL;
	}

	LogInformation("Post-Hook removed from %p", TargetFunction);
	VirtualMemory::FreeNonPagedPool(Entry);
	return TRUE;
}

VOID
EptHook::RevertPostHookFrames(PEPT_POST_HOOK_ENTRY TargetEntry)
{
	PPOST_HOOK_BUCKET Buckets = EptHook::PostHookBuckets;
	ULONG BucketCount = EptHook::PostHookBucketCount;
	if (Buckets == NULL || BucketCount == 0)
	{
		return;
	}

	for (ULONG b = 0; b < BucketCount; b++)
	{
		PPOST_HOOK_BUCKET Bucket = &Buckets[b];
		Util::AcquireLock(&Bucket->Lock);

		PPOST_HOOK_CALL_FRAME Prev = NULL;
		PPOST_HOOK_CALL_FRAME Frame = Bucket->Head;
		while (Frame != NULL)
		{
			PPOST_HOOK_CALL_FRAME Next = Frame->Next;
			if (TargetEntry == NULL || Frame->Entry == TargetEntry)
			{
				__try
				{
					if (Frame->ReturnRipSlot != NULL && *Frame->ReturnRipSlot == (PVOID)AsmPostHookReturnStub)
					{
						*Frame->ReturnRipSlot = (PVOID)Frame->OriginalReturnRip;
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					LogError("Exception reverting frame ReturnRipSlot %p", Frame->ReturnRipSlot);
				}
				if (Prev == NULL)
				{
					Bucket->Head = Next;
				}
				else
				{
					Prev->Next = Next;
				}
				RtlZeroMemory(Frame, sizeof(POST_HOOK_CALL_FRAME));
				InterlockedPushEntrySList(&EptHook::FreeFrameListHead, &Frame->SListEntry);
			}
			else
			{
				Prev = Frame;
			}
			Frame = Next;
		}
		Util::ReleaseLock(&Bucket->Lock);
	}
}

VOID
EptHook::UnhookAll()
{
	if (!EptHook::Initialized)
	{
		return;
	}

	EptHook::RevertPostHookFrames(NULL);

	while (!IsListEmpty(&EptHook::PostHookList))
	{
		PLIST_ENTRY Flink = EptHook::PostHookList.Flink;
		PEPT_POST_HOOK_ENTRY PostEntry = CONTAINING_RECORD(Flink, EPT_POST_HOOK_ENTRY, ListEntry);
		EptHook::UnhookPostHook(PostEntry->TargetFunction);
	}

	while (!IsListEmpty(&EptHook::HookList))
	{
		PLIST_ENTRY Flink = EptHook::HookList.Flink;
		PEPT_HOOK_ENTRY Entry = CONTAINING_RECORD(Flink, EPT_HOOK_ENTRY, ListEntry);
		EptHook::Unhook(Entry->TargetVirtualAddress);
	}

	LogInformation("All EPT Hooks and Post-Hooks unhooked successfully");
	return;
}
#pragma endregion

#pragma region Find
PEPT_HOOK_ENTRY
EptHook::FindHookEntryByPfn(ULONG64 Pfn)
{
	PLIST_ENTRY Current = EptHook::HookList.Flink;
	while (Current != &EptHook::HookList)
	{
		PEPT_HOOK_ENTRY Entry = CONTAINING_RECORD(Current, EPT_HOOK_ENTRY, ListEntry);
		if (Entry->OriginalPfn == Pfn || Entry->FakePfn == Pfn)
		{
			return Entry;
		}
		Current = Current->Flink;
	}

	return NULL;
}

PEPT_HOOK_ENTRY
EptHook::FindHookEntryByAddress(PVOID TargetAddress)
{
	PLIST_ENTRY Current = EptHook::HookList.Flink;
	while (Current != &EptHook::HookList)
	{
		PEPT_HOOK_ENTRY Entry = CONTAINING_RECORD(Current, EPT_HOOK_ENTRY, ListEntry);
		if (Entry->TargetVirtualAddress == TargetAddress)
		{
			return Entry;
		}
		Current = Current->Flink;
	}

	return NULL;
}


PEPT_POST_HOOK_ENTRY
EptHook::FindPostHookEntryByAddress(PVOID TargetAddress)
{
	PLIST_ENTRY Current = EptHook::PostHookList.Flink;
	while (Current != &EptHook::PostHookList)
	{
		PEPT_POST_HOOK_ENTRY Entry = CONTAINING_RECORD(Current, EPT_POST_HOOK_ENTRY, ListEntry);
		if (Entry->TargetFunction == TargetAddress)
		{
			return Entry;
		}
		Current = Current->Flink;
	}

	return NULL;
}
#pragma endregion

#pragma region Hook
NTSTATUS
EptHook::Hook(PVOID TargetFunction, PVOID HookHandler, PVOID* OriginalFunction)
{
	if (!EptHook::Initialized || !TargetFunction || !HookHandler)
	{
		LogWarning("Invalid Parameter");
		return STATUS_INVALID_PARAMETER;
	}

	if (EptHook::FindHookEntryByAddress(TargetFunction) != NULL)
	{
		LogWarning("Target function %p is already hooked", TargetFunction);
		return STATUS_ALREADY_COMMITTED;
	}

	ULONG64 PhysicalAddress = VirtualMemory::VirtualAddressToPhysicalAddress(TargetFunction);
	if (!PhysicalAddress)
	{
		LogError("Failed to resolve physical address for %p", TargetFunction);
		return STATUS_UNSUCCESSFUL;
	}

	ULONG64 OriginalPfn = EPT_PAGE_FRAME_NUMBER(PhysicalAddress);
	ULONG PageOffset = EPT_PAGE_OFFSET(PhysicalAddress);

	PEPT_PTE TargetPte = Ept::GetPte(PhysicalAddress);
	if (TargetPte == NULL)
	{
		LogError("Failed to obtain 4KB EPT PTE for physical address 0x%llx", PhysicalAddress);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PEPT_HOOK_ENTRY Entry = NULL;
	if (!VirtualMemory::AllocateNonPagedPool((PVOID*)&Entry, sizeof(EPT_HOOK_ENTRY), FALSE))
	{
		LogError("Failed to allocate EPT_HOOK_ENTRY");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlZeroMemory(Entry, sizeof(EPT_HOOK_ENTRY));

	hde64s Hde64 = { 0 };
	ULONG EptHookJmpSize = 0;
	while (EptHookJmpSize < EPT_HOOK_JMP_SIZE)
	{
		ULONG Len = hde64_disasm((PUCHAR)TargetFunction + EptHookJmpSize, &Hde64);
		if (Len == 0 || (Hde64.flags & F_ERROR))
		{
			LogError("Failed to disassemble instruction at %p", (PUCHAR)TargetFunction + EptHookJmpSize);
			VirtualMemory::FreeNonPagedPool(Entry);
			return STATUS_UNSUCCESSFUL;
		}
		EptHookJmpSize += Len;
	}

	if (!VirtualMemory::AllocateNonPagedPool((PVOID*)&Entry->OriginalBytes, EptHookJmpSize, FALSE))
	{
		LogError("Failed to allocate Original Data Point");
		VirtualMemory::FreeNonPagedPool(Entry);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	Entry->TargetVirtualAddress = TargetFunction;
	Entry->TargetPhysicalAddress = PhysicalAddress;
	Entry->OriginalPfn = OriginalPfn;
	Entry->PageOffset = PageOffset;
	Entry->TargetPte = TargetPte;
	Entry->HookHandler = HookHandler;
	Entry->OriginalBytesSize = EptHookJmpSize;
	RtlCopyMemory(Entry->OriginalBytes, TargetFunction, EptHookJmpSize);

	// Check if this physical page is already hooked by an existing entry
	PEPT_HOOK_ENTRY ExistingPageEntry = EptHook::FindHookEntryByPfn(OriginalPfn);
	if (ExistingPageEntry != NULL)
	{
		// Reuse existing fake page
		Entry->FakePageVirtualAddress = ExistingPageEntry->FakePageVirtualAddress;
		Entry->FakePfn = ExistingPageEntry->FakePfn;
	}
	else
	{
		Entry->FakePageVirtualAddress = PhysicalMemory::AllocateContiguousMemory(PAGE_SIZE);
		if (Entry->FakePageVirtualAddress == NULL)
		{
			VirtualMemory::FreeNonPagedPool(Entry->OriginalBytes);
			VirtualMemory::FreeNonPagedPool(Entry);
			LogError("Failed to allocate shadow 4KB physical page");
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		// Copy original page contents into shadow page
		PVOID AlignedTargetVa = PAGE_ALIGN(TargetFunction);
		RtlCopyMemory(Entry->FakePageVirtualAddress, AlignedTargetVa, PAGE_SIZE);

		ULONG64 FakePhysicalAddress = VirtualMemory::VirtualAddressToPhysicalAddress(Entry->FakePageVirtualAddress);
		Entry->FakePfn = EPT_PAGE_FRAME_NUMBER(FakePhysicalAddress);
	}

	// Create Trampoline if requested
	if (OriginalFunction != NULL)
	{
		ULONG TrampolineSize = EptHookJmpSize + sizeof(ABSOLUTE_JUMP_14);
		PVOID TrampolineBuffer = NULL;
		if (VirtualMemory::AllocateNonPagedPool(&TrampolineBuffer, TrampolineSize, TRUE))
		{
			RtlCopyMemory(TrampolineBuffer, TargetFunction, EptHookJmpSize);
			PABSOLUTE_JUMP_14 JumpBack = (PABSOLUTE_JUMP_14)((PUCHAR)TrampolineBuffer + EptHookJmpSize);
			JumpBack->Opcode[0] = 0xFF;
			JumpBack->Opcode[1] = 0x25;
			JumpBack->Opcode[2] = 0x00;
			JumpBack->Opcode[3] = 0x00;
			JumpBack->Opcode[4] = 0x00;
			JumpBack->Opcode[5] = 0x00;
			JumpBack->TargetAddress = ((ULONG64)TargetFunction) + EptHookJmpSize;

			Entry->OriginalTrampoline = TrampolineBuffer;
			Entry->TrampolineSize = TrampolineSize;
			*OriginalFunction = TrampolineBuffer;
		}
		else
		{
			LogWarning("Failed to allocate trampoline memory for %p", TargetFunction);
			*OriginalFunction = NULL;
			if (ExistingPageEntry == NULL)
			{
				PhysicalMemory::FreeContiguousMemory(Entry->FakePageVirtualAddress);
			}
			VirtualMemory::FreeNonPagedPool(Entry->OriginalBytes);
			VirtualMemory::FreeNonPagedPool(Entry);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}

	// Write 12-byte absolute jump to HookHandler on shadow page (mov rax, HookHandler; jmp rax)
	PMOV_RAX_JMP_RAX_12 ShadowJump = (PMOV_RAX_JMP_RAX_12)((PUCHAR)Entry->FakePageVirtualAddress + PageOffset);
	ShadowJump->MovOpcode[0] = 0x48;	// mov rax, HookHandler
	ShadowJump->MovOpcode[1] = 0xB8;
	ShadowJump->TargetAddress = (ULONG64)HookHandler;
	ShadowJump->JmpOpcode[0] = 0xFF;	// jmp rax
	ShadowJump->JmpOpcode[1] = 0xE0;

	TargetPte->Fields.PageFrameNumber = Entry->FakePfn;
	TargetPte->Fields.Read = 0;
	TargetPte->Fields.Write = 0;
	TargetPte->Fields.Execute = 1;

	Entry->IsHooked = TRUE;

	InsertTailList(&EptHook::HookList, &Entry->ListEntry);

	if (Vcpu::VcpuState && Vcpu::VcpuState[0].VmcsLaunchStatus)
	{
		KeIpiGenericCall(Ept::InveptSingleContext, 0);
	}

	LogInformation("EPT Hook placed on %p -> Handler : %p (Original PFN: 0x%llx, Fake PFN: 0x%llx)",
		TargetFunction, HookHandler, OriginalPfn, Entry->FakePfn);

	return STATUS_SUCCESS;
}

EXTERN_C PVOID PostHookOnEntry(
	PEPT_POST_HOOK_ENTRY Entry,
	PVOID* pCallerReturnRipSlot,
	ULONG64 CallerRsp,
	PULONG64 pSavedArgs
)
{
	if (Entry == NULL || pCallerReturnRipSlot == NULL || CallerRsp == NULL)
	{
		LogError("Invalid Parameter (Entry: %p, ReturnRipSlot: %p, CallerRsp: %llx)",
			Entry, pCallerReturnRipSlot, CallerRsp);
		NT_ASSERT(Entry != NULL);
		return (Entry != NULL) ? Entry->OriginalTrampoline : NULL;
	}

	if (HookGuard::IsQuiescing())
	{
		return Entry->OriginalTrampoline;
	}

	ULONG64 OriginalReturnRip = (ULONG64)*pCallerReturnRipSlot;
	PVOID CurrentThread = PsGetCurrentThread();

	GUEST_CONTEXT Context = { 0 };
	if (pSavedArgs != NULL)
	{
		Context.Rax = pSavedArgs[1];
		Context.Rcx = pSavedArgs[0];
		Context.Rdx = pSavedArgs[-1];
		Context.R8  = pSavedArgs[-2];
		Context.R9  = pSavedArgs[-3];
	}
	Context.GuestRip = OriginalReturnRip;
	Context.GuestRsp = CallerRsp;

	BOOLEAN ShouldSkip = FALSE;

	if (Entry->PreHookFunction != NULL)
	{
		ULONG GuardSlot = HOOK_GUARD_INVALID_SLOT;
		if (HookGuard::Enter(Entry->TargetFunction, &GuardSlot))
		{
			if (Entry->UsePreHookResult)
			{
				fnPreHookWithResult PreHookFn = (fnPreHookWithResult)Entry->PreHookFunction;
				ShouldSkip = PreHookFn(&Context);
			}
			else
			{
				fnPreHookCallback PreHookFn = (fnPreHookCallback)Entry->PreHookFunction;
				PreHookFn(&Context);
	
			}

			if (pSavedArgs != NULL)
			{
				pSavedArgs[1]  = Context.Rax;
				pSavedArgs[0]  = Context.Rcx;
				pSavedArgs[-1] = Context.Rdx;
				pSavedArgs[-2] = Context.R8;
				pSavedArgs[-3] = Context.R9;
			}

			HookGuard::Leave(GuardSlot);
		}
	}

	if (ShouldSkip)
	{
		*pCallerReturnRipSlot = (PVOID)OriginalReturnRip;
		return (PVOID)AsmCommonRetStub;
	}

	if (Entry->HookFunction != NULL)
	{
		PPOST_HOOK_BUCKET Buckets = EptHook::PostHookBuckets;
		if (Buckets == NULL)
		{
			LogError("PostHookBuckets is NULL");
			return Entry->OriginalTrampoline;
		}

		PSLIST_ENTRY SListEntry = InterlockedPopEntrySList(&EptHook::FreeFrameListHead);
		if (SListEntry == NULL)
		{
			if (!EptHook::AllocateMoreFrames(POST_HOOK_CHUNK_FRAMES))
			{
				LogWarning("Dynamic call frame allocation failed for thread %p", CurrentThread);
				return Entry->OriginalTrampoline;
			}

			SListEntry = InterlockedPopEntrySList(&EptHook::FreeFrameListHead);
			if (SListEntry == NULL)
			{
				return Entry->OriginalTrampoline;
			}
		}

		PPOST_HOOK_CALL_FRAME Frame = CONTAINING_RECORD(SListEntry, POST_HOOK_CALL_FRAME, SListEntry);
		Frame->Thread = CurrentThread;
		Frame->CallerRsp = CallerRsp;
		Frame->OriginalReturnRip = OriginalReturnRip;
		Frame->ReturnRipSlot = pCallerReturnRipSlot;
		Frame->Entry = Entry;

		Frame->Rcx = Context.Rcx;
		Frame->Rdx = Context.Rdx;
		Frame->R8  = Context.R8;
		Frame->R9  = Context.R9;

		ULONG BucketIndex = PostHookHash(CurrentThread);
		PPOST_HOOK_BUCKET Bucket = &Buckets[BucketIndex];

		Util::AcquireLock(&Bucket->Lock);
		Frame->Next = Bucket->Head;
		Bucket->Head = Frame;
		Util::ReleaseLock(&Bucket->Lock);

		*pCallerReturnRipSlot = (PVOID)AsmPostHookReturnStub;
	}

	return Entry->OriginalTrampoline;
}

EXTERN_C ULONG64 PostHookOnReturn(
	ULONG64 ReturnValue,
	ULONG64 CurrentRsp,
	PULONG64 pOutOriginalReturnRip,
	PVOID Reserved
)
{
	UNREFERENCED_PARAMETER(Reserved);

	PVOID CurrentThread = PsGetCurrentThread();
	ULONG64 OriginalReturnRip = 0;
	PEPT_POST_HOOK_ENTRY Entry = NULL;
	GUEST_CONTEXT Context = { 0 };

	PPOST_HOOK_BUCKET Buckets = EptHook::PostHookBuckets;
	if (Buckets == NULL)
	{
		LogError("PostHookBuckets is NULL for thread %p", CurrentThread);
		if (pOutOriginalReturnRip != NULL)
		{
			*pOutOriginalReturnRip = 0;
		}
		KeBugCheckEx(CRITICAL_STRUCTURE_CORRUPTION, (ULONG_PTR)CurrentThread, (ULONG_PTR)CurrentRsp, 0, 0x504F5354);
	}

	ULONG BucketIndex = PostHookHash(CurrentThread);
	PPOST_HOOK_BUCKET Bucket = &Buckets[BucketIndex];

	Util::AcquireLock(&Bucket->Lock);

	PPOST_HOOK_CALL_FRAME Prev = NULL;
	PPOST_HOOK_CALL_FRAME Curr = Bucket->Head;
	PPOST_HOOK_CALL_FRAME MatchedFrame = NULL;
	PPOST_HOOK_CALL_FRAME MatchedPrev = NULL;

	// Match by both Thread and exact CallerRsp
	while (Curr != NULL)
	{
		if (Curr->Thread == CurrentThread && Curr->CallerRsp == CurrentRsp)
		{
			MatchedFrame = Curr;
			MatchedPrev = Prev;
			break;
		}
		Prev = Curr;
		Curr = Curr->Next;
	}

	// Fallback to newest frame for this thread if exact RSP not found
	if (MatchedFrame == NULL)
	{
		Prev = NULL;
		Curr = Bucket->Head;
		while (Curr != NULL)
		{
			if (Curr->Thread == CurrentThread)
			{
				MatchedFrame = Curr;
				MatchedPrev = Prev;
				break;
			}
			Prev = Curr;
			Curr = Curr->Next;
		}
	}

	if (MatchedFrame != NULL)
	{
		if (MatchedPrev == NULL)
		{
			Bucket->Head = MatchedFrame->Next;
		}
		else
		{
			MatchedPrev->Next = MatchedFrame->Next;
		}
		MatchedFrame->Next = NULL;
	}

	Util::ReleaseLock(&Bucket->Lock);

	if (MatchedFrame != NULL)
	{
		OriginalReturnRip = MatchedFrame->OriginalReturnRip;
		Entry = MatchedFrame->Entry;

		Context.Rcx = MatchedFrame->Rcx;
		Context.Rdx = MatchedFrame->Rdx;
		Context.R8  = MatchedFrame->R8;
		Context.R9  = MatchedFrame->R9;
		Context.Rax = ReturnValue;
		Context.GuestRip = OriginalReturnRip;
		Context.GuestRsp = CurrentRsp;

		// Recycle frame back to lock-free free list O(1)
		RtlZeroMemory(MatchedFrame, sizeof(POST_HOOK_CALL_FRAME));
		InterlockedPushEntrySList(&EptHook::FreeFrameListHead, &MatchedFrame->SListEntry);
	}
	else
	{
		LogError("Failed to find call frame for thread %p RSP %llx", CurrentThread, CurrentRsp);
	}

	if (pOutOriginalReturnRip != NULL)
	{
		*pOutOriginalReturnRip = OriginalReturnRip;

		if (OriginalReturnRip == 0)
		{
			LogError("OriginalReturnRip is 0 for thread %p RSP %llx", CurrentThread, CurrentRsp);
			NT_ASSERT(OriginalReturnRip != 0);
			KeBugCheckEx(CRITICAL_STRUCTURE_CORRUPTION, (ULONG_PTR)CurrentThread, (ULONG_PTR)CurrentRsp, 0, 0x504F5354);
		}
	}

	ULONG64 FinalResult = ReturnValue;
	if (Entry != NULL && Entry->HookFunction != NULL)
	{
		ULONG GuardSlot = HOOK_GUARD_INVALID_SLOT;
		if (HookGuard::Enter(Entry->TargetFunction, &GuardSlot))
		{
			fnPostHookCallback HookFn = (fnPostHookCallback)Entry->HookFunction;
			FinalResult = HookFn(&Context, ReturnValue);
			HookGuard::Leave(GuardSlot);
		}
	}

	return FinalResult;
}

BOOLEAN
EptHook::PostHook(
	PVOID TargetFunction,
	fnPreHookCallback PreHookFunction,
	fnPostHookCallback PostHookFunction,
	PVOID* OriginalFunction,
	BOOLEAN UsePreHookResult
)
{
	if (!EptHook::Initialized || !TargetFunction || (!PostHookFunction && !PreHookFunction))
	{
		LogWarning("Invalid Parameter");
		return FALSE;
	}

	if (EptHook::FindPostHookEntryByAddress(TargetFunction) != NULL)
	{
		LogWarning("Target function %p is already post-hooked", TargetFunction);
		return FALSE;
	}

	PEPT_POST_HOOK_ENTRY Entry = NULL;
	if (!VirtualMemory::AllocateNonPagedPool((PVOID*)&Entry, sizeof(EPT_POST_HOOK_ENTRY), FALSE))
	{
		LogError("Failed to allocate EPT_POST_HOOK_ENTRY");
		return FALSE;
	}
	RtlZeroMemory(Entry, sizeof(EPT_POST_HOOK_ENTRY));

	// Allocate 24-byte executable entry thunk:
	//   mov r10, Entry                  ; 49 BA [Entry] (10 bytes)
	//   mov r11, AsmPostHookCommonEntry ; 49 BB [AsmPostHookCommonEntry] (10 bytes)
	//   jmp r11                         ; 41 FF E3 (3 bytes)
	//   nop                             ; 90 (1 byte)
	PVOID EntryThunk = NULL;
	if (!VirtualMemory::AllocateNonPagedPool(&EntryThunk, 24, TRUE))
	{
		LogError("Failed to allocate executable EntryThunk");
		VirtualMemory::FreeNonPagedPool(Entry);
		return FALSE;
	}

	PUCHAR ThunkCode = (PUCHAR)EntryThunk;
	ThunkCode[0] = 0x49;	// mov r10, Entry
	ThunkCode[1] = 0xBA;
	*(PULONG64)&ThunkCode[2] = (ULONG64)Entry;

	ThunkCode[10] = 0x49;	// mov r11, AsmPostHookCommonEntry
	ThunkCode[11] = 0xBB;
	*(PULONG64)&ThunkCode[12] = (ULONG64)AsmPostHookCommonEntry;

	ThunkCode[20] = 0x41;	// jmp r11
	ThunkCode[21] = 0xFF;
	ThunkCode[22] = 0xE3;
	ThunkCode[23] = 0x90;	// nop

	Entry->TargetFunction = TargetFunction;
	Entry->HookFunction = (PVOID)PostHookFunction;
	Entry->PreHookFunction = (PVOID)PreHookFunction;
	Entry->EntryThunk = EntryThunk;
	Entry->UsePreHookResult = UsePreHookResult;

	// Install stealth EPT hook pointing to EntryThunk
	NTSTATUS Status = EptHook::Hook(TargetFunction, EntryThunk, &Entry->OriginalTrampoline);
	if (!NT_SUCCESS(Status))
	{
		LogError("EptHook::Hook failed with status 0x%08X", Status);
		VirtualMemory::FreeNonPagedPool(EntryThunk);
		VirtualMemory::FreeNonPagedPool(Entry);
		return FALSE;
	}

	if (OriginalFunction != NULL)
	{
		*OriginalFunction = Entry->OriginalTrampoline;
	}

	Entry->IsHooked = TRUE;
	InsertTailList(&EptHook::PostHookList, &Entry->ListEntry);

	LogInformation("Post-Hook installed on %p -> PostHook: %p, PreHook: %p (Thunk: %p, Trampoline: %p)",
		TargetFunction, PostHookFunction, PreHookFunction, EntryThunk, Entry->OriginalTrampoline);

	return TRUE;
}

BOOLEAN
EptHook::PostHook(
	PVOID TargetFunction,
	fnPreHookFilterCallback PreHookFilterFunction,
	fnPostHookCallback PostHookFunction,
	PVOID* OriginalFunction
)
{
	return EptHook::PostHook(TargetFunction, (fnPreHookCallback)PreHookFilterFunction, PostHookFunction, OriginalFunction, TRUE);
}

BOOLEAN
EptHook::PostHook(
	PVOID TargetFunction,
	fnPostHookCallback PostHookFunction
)
{
	return EptHook::PostHook(TargetFunction, NULL, PostHookFunction, NULL, FALSE);
}

BOOLEAN
EptHook::UtilHook(
	PVOID TargetFunction,
	fnPreHookCallback PreHookFunction,
	fnPostHookCallback PostHookFunction,
	PVOID* OriginalFunction,
	BOOLEAN UsePreHookResult
)
{
	return EptHook::PostHook(TargetFunction, PreHookFunction, PostHookFunction, OriginalFunction, UsePreHookResult);
}

BOOLEAN
EptHook::UtilHook(
	PVOID TargetFunction,
	fnPreHookFilterCallback PreHookFilterFunction,
	fnPostHookCallback PostHookFunction,
	PVOID* OriginalFunction
)
{
	return EptHook::PostHook(TargetFunction, PreHookFilterFunction, PostHookFunction, OriginalFunction);
}

BOOLEAN
EptHook::UtilHook(
	PVOID TargetFunction,
	fnPostHookCallback PostHookFunction
)
{
	return EptHook::PostHook(TargetFunction, PostHookFunction);
}
#pragma endregion

#pragma region VM-Exit-Handler
BOOLEAN
EptHook::HandleEptViolation(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress, EPT_VIOLATION_QUALIFICATION Qualification)
{
	UNREFERENCED_PARAMETER(Context);

	ULONG64 Pfn = EPT_PAGE_FRAME_NUMBER(GuestPhysicalAddress);
	ULONG CurrentCpu = KeGetCurrentProcessorNumberEx(NULL);
	PVCPU_STATE CurrentVcpu = &Vcpu::VcpuState[CurrentCpu];

	PEPT_HOOK_ENTRY Entry = EptHook::FindHookEntryByPfn(Pfn);
	if (Entry == NULL)
	{
		return FALSE;
	}

	// Read or Write violation on Fake Page (PatchGuard / memory scanner reading code)
	if (Qualification.Fields.ReadAccess || Qualification.Fields.WriteAccess)
	{
		Util::AcquireLock(&EptHook::EptViolationLock);

		// Temporarily swap to Original Page (rwx), so caller reads genuine unhooked code
		Entry->TargetPte->Fields.PageFrameNumber = Entry->OriginalPfn;
		Entry->TargetPte->Fields.Read = 1;
		Entry->TargetPte->Fields.Write = 1;
		Entry->TargetPte->Fields.Execute = 1;

		Ept::InveptSingleContext();

		Vmcs::EnableMonitorTrapFlag();

		CurrentVcpu->CurrentActiveHook = Entry;

		return TRUE;
	}

	// Execute violation (defensive restore to Fake Page --x)
	if (Qualification.Fields.ExecuteAccess)
	{
		Util::AcquireLock(&EptHook::EptViolationLock);

		Entry->TargetPte->Fields.PageFrameNumber = Entry->FakePfn;
		Entry->TargetPte->Fields.Read = 0;
		Entry->TargetPte->Fields.Write = 0;
		Entry->TargetPte->Fields.Execute = 1;

		Ept::InveptSingleContext();

		Vmcs::DisableMonitorTrapFlag();

		CurrentVcpu->CurrentActiveHook = NULL;

		Util::ReleaseLock(&EptHook::EptViolationLock);

		return TRUE;
	}

	return FALSE;
}

BOOLEAN
EptHook::HandleMonitorTrapFlag(PGUEST_CONTEXT Context)
{
	UNREFERENCED_PARAMETER(Context);

	ULONG CurrentCpu = KeGetCurrentProcessorNumberEx(NULL);
	PVCPU_STATE CurrentVcpu = &Vcpu::VcpuState[CurrentCpu];

	// Pre-Hook, MTF restore
	PEPT_HOOK_ENTRY Entry = (PEPT_HOOK_ENTRY)CurrentVcpu->CurrentActiveHook;
	if (Entry != NULL && Entry->TargetPte != NULL)
	{
		Entry->TargetPte->Fields.PageFrameNumber = Entry->FakePfn;
		Entry->TargetPte->Fields.Read = 0;
		Entry->TargetPte->Fields.Write = 0;
		Entry->TargetPte->Fields.Execute = 1;

		Ept::InveptSingleContext();

		CurrentVcpu->CurrentActiveHook = NULL;
	}

	Vmcs::DisableMonitorTrapFlag();

	// Release lock now that PTE is restored to stealth state
	Util::ReleaseLock(&EptHook::EptViolationLock);

	return TRUE;
}
#pragma endregion


