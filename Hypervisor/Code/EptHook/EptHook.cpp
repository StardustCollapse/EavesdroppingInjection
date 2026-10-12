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
SLIST_HEADER EptHook::FreeFrameListHead = { 0 };
PPOST_HOOK_CALL_FRAME EptHook::PostHookFramePool = NULL;
POST_HOOK_BUCKET EptHook::PostHookBuckets[POST_HOOK_HASH_BUCKETS] = { 0 };
volatile LONG alignas(64) EptHook::EptViolationLock = 0;

BOOLEAN
EptHook::Initialize()
{
	if (EptHook::Initialized)
	{
		return TRUE;
	}

	InitializeListHead(&EptHook::HookList);
	InitializeListHead(&EptHook::PostHookList);

	PVOID PoolBuffer = NULL;
	if (!VirtualMemory::AllocateNonPagedPool(&PoolBuffer, sizeof(POST_HOOK_CALL_FRAME) * POST_HOOK_MAX_FRAMES, FALSE))
	{
		LogError("Initialize: Failed to allocate PostHookFramePool");
		return FALSE;
	}
	RtlZeroMemory(PoolBuffer, sizeof(POST_HOOK_CALL_FRAME) * POST_HOOK_MAX_FRAMES);
	EptHook::PostHookFramePool = (PPOST_HOOK_CALL_FRAME)PoolBuffer;

	InitializeSListHead(&EptHook::FreeFrameListHead);
	for (ULONG i = 0; i < POST_HOOK_MAX_FRAMES; i++)
	{
		InterlockedPushEntrySList(&EptHook::FreeFrameListHead, &EptHook::PostHookFramePool[i].SListEntry);
	}

	// Initialize Hash Buckets
	for (ULONG i = 0; i < POST_HOOK_HASH_BUCKETS; i++)
	{
		EptHook::PostHookBuckets[i].Lock = 0;
		EptHook::PostHookBuckets[i].Head = NULL;
	}

	EptHook::EptViolationLock = 0;
	EptHook::Initialized = TRUE;

	LogInformation("EPT Hook initialized successfully (Post-Hook Hash Table: %d buckets, %d preallocated frames)",
		POST_HOOK_HASH_BUCKETS, POST_HOOK_MAX_FRAMES);
	return TRUE;
}

VOID
EptHook::Destroy()
{
	if (!EptHook::Initialized)
	{
		return;
	}

	EptHook::UnhookAll();

	if (EptHook::PostHookFramePool != NULL)
	{
		VirtualMemory::FreeNonPagedPool(EptHook::PostHookFramePool);
		EptHook::PostHookFramePool = NULL;
	}

	EptHook::Initialized = FALSE;
	LogInformation("EPT Hook destroyed");
	return;
}

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

	ULONG64 OriginalPfn = PhysicalAddress >> 12;
	ULONG PageOffset = (ULONG)(PhysicalAddress & 0xFFFULL);

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
		Entry->FakePfn = FakePhysicalAddress >> 12;
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
		KeIpiGenericCall(Ept::InveptAllContexts, 0);
	}

	LogInformation("EPT Hook placed on %p -> Handler : %p (Original PFN: 0x%llx, Fake PFN: 0x%llx)",
		TargetFunction, HookHandler, OriginalPfn, Entry->FakePfn);

	return STATUS_SUCCESS;
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
			KeIpiGenericCall(Ept::InveptAllContexts, 0);
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

EXTERN_C PVOID PostHookOnEntry(
	PEPT_POST_HOOK_ENTRY Entry,
	PVOID* pCallerReturnRipSlot,
	ULONG64 CallerRsp,
	PULONG64 pSavedArgs
)
{
	if (Entry == NULL || pCallerReturnRipSlot == NULL)
	{
		return NULL;
	}

	if (HookGuard::IsQuiescing())
	{
		return Entry->OriginalTrampoline;
	}

	ULONG64 OriginalReturnRip = (ULONG64)*pCallerReturnRipSlot;
	PVOID CurrentThread = PsGetCurrentThread();

	// Atomic lock-free pop from free frame pool O(1)
	PSLIST_ENTRY SListEntry = InterlockedPopEntrySList(&EptHook::FreeFrameListHead);
	if (SListEntry == NULL)
	{
		LogWarning("PostHookOnEntry : Call frame pool exhausted for thread %p", CurrentThread);
		return Entry->OriginalTrampoline;
	}

	PPOST_HOOK_CALL_FRAME Frame = CONTAINING_RECORD(SListEntry, POST_HOOK_CALL_FRAME, SListEntry);
	Frame->Thread = CurrentThread;
	Frame->CallerRsp = CallerRsp;
	Frame->OriginalReturnRip = OriginalReturnRip;
	Frame->Entry = Entry;

	if (pSavedArgs != NULL)
	{
		Frame->Rcx = pSavedArgs[0];
		Frame->Rdx = pSavedArgs[-1];
		Frame->R8  = pSavedArgs[-2];
		Frame->R9  = pSavedArgs[-3];
	}
	else
	{
		Frame->Rcx = Frame->Rdx = Frame->R8 = Frame->R9 = 0;
	}

	// Insert into hash bucket (O(1) prepend under bucket lock)
	ULONG BucketIndex = PostHookHash(CurrentThread);
	PPOST_HOOK_BUCKET Bucket = &EptHook::PostHookBuckets[BucketIndex];

	Util::AcquireLock(&Bucket->Lock);
	Frame->Next = Bucket->Head;
	Bucket->Head = Frame;
	Util::ReleaseLock(&Bucket->Lock);

	// Rewrite stack return address to AsmPostHookReturnStub!
	*pCallerReturnRipSlot = (PVOID)AsmPostHookReturnStub;

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

	ULONG BucketIndex = PostHookHash(CurrentThread);
	PPOST_HOOK_BUCKET Bucket = &EptHook::PostHookBuckets[BucketIndex];

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
		LogError("PostHookOnReturn : Failed to find call frame for thread %p RSP %llx", CurrentThread, CurrentRsp);
	}

	if (pOutOriginalReturnRip != NULL)
	{
		*pOutOriginalReturnRip = OriginalReturnRip;
	}

	ULONG64 FinalResult = ReturnValue;
	if (Entry != NULL && Entry->HookFunction != NULL)
	{
		fnPostHookCallback HookFn = (fnPostHookCallback)Entry->HookFunction;
		FinalResult = HookFn(&Context, ReturnValue);
	}

	return FinalResult;
}

BOOLEAN
EptHook::PostHook(PVOID TargetFunction, PVOID HookFunction)
{
	if (!EptHook::Initialized || !TargetFunction || !HookFunction)
	{
		LogWarning("PostHook : Invalid Parameter");
		return FALSE;
	}

	if (EptHook::FindPostHookEntryByAddress(TargetFunction) != NULL)
	{
		LogWarning("PostHook : Target function %p is already post-hooked", TargetFunction);
		return FALSE;
	}

	PEPT_POST_HOOK_ENTRY Entry = NULL;
	if (!VirtualMemory::AllocateNonPagedPool((PVOID*)&Entry, sizeof(EPT_POST_HOOK_ENTRY), FALSE))
	{
		LogError("PostHook : Failed to allocate EPT_POST_HOOK_ENTRY");
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
		LogError("PostHook : Failed to allocate executable EntryThunk");
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
	Entry->HookFunction = HookFunction;
	Entry->EntryThunk = EntryThunk;

	// Install stealth EPT hook pointing to EntryThunk
	NTSTATUS Status = EptHook::Hook(TargetFunction, EntryThunk, &Entry->OriginalTrampoline);
	if (!NT_SUCCESS(Status))
	{
		LogError("PostHook: EptHook::Hook failed with status 0x%08X", Status);
		VirtualMemory::FreeNonPagedPool(EntryThunk);
		VirtualMemory::FreeNonPagedPool(Entry);
		return FALSE;
	}

	Entry->IsHooked = TRUE;
	InsertTailList(&EptHook::PostHookList, &Entry->ListEntry);

	LogInformation("Post-Hook (Scheme A) installed on %p -> HookFunction: %p (Thunk: %p, Trampoline: %p)",
		TargetFunction, HookFunction, EntryThunk, Entry->OriginalTrampoline);

	return TRUE;
}

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

	EptHook::Unhook(TargetFunction);

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
EptHook::UnhookAll()
{
	if (!EptHook::Initialized)
	{
		return;
	}

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

BOOLEAN
EptHook::HandleEptViolation(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress, EPT_VIOLATION_QUALIFICATION Qualification)
{
	UNREFERENCED_PARAMETER(Context);

	ULONG64 Pfn = GuestPhysicalAddress >> 12;
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

		INVEPT_DESCRIPTOR Descriptor = { 0 };
		AsmInvept(INVEPT_ALL_CONTEXTS, &Descriptor);

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

		INVEPT_DESCRIPTOR Descriptor = { 0 };
		AsmInvept(INVEPT_ALL_CONTEXTS, &Descriptor);

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

		INVEPT_DESCRIPTOR Descriptor = { 0 };
		AsmInvept(INVEPT_ALL_CONTEXTS, &Descriptor);

		CurrentVcpu->CurrentActiveHook = NULL;
	}

	Vmcs::DisableMonitorTrapFlag();

	// Release lock now that PTE is restored to stealth state
	Util::ReleaseLock(&EptHook::EptViolationLock);

	return TRUE;
}


