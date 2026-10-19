#include "EptHook/HookGuard.h"
#include "Memory/VirtualMemory.h"
#include "Log/Log.h"

PHOOK_GUARD_SLOT HookGuard::m_Slots = NULL;
ULONG HookGuard::m_Capacity = 0;
ULONG HookGuard::m_Mask = 0;
BOOLEAN HookGuard::m_Initialized = FALSE;
volatile LONG HookGuard::m_ActiveHookCount = 0;
volatile BOOLEAN HookGuard::m_Stopping = FALSE;

#define HOOK_GUARD_PROBE_DEPTH 8

BOOLEAN
HookGuard::Initialize(ULONG64 LogicalProcessorsCount)
{
	if (m_Initialized)
	{
		return TRUE;
	}

	if (LogicalProcessorsCount == 0)
	{
		LogicalProcessorsCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	}

	// 32 slots per logical processor from Vmm, minimum 256
	ULONG DesiredSlots = (ULONG)LogicalProcessorsCount * 32;
	if (DesiredSlots < 256)
	{
		DesiredSlots = 256;
	}

	// Round up to next power of 2 for fast bitwise masking (Mask = Capacity - 1)
	ULONG Capacity = 256;
	while (Capacity < DesiredSlots)
	{
		Capacity <<= 1;
	}

	ULONG AllocationSize = Capacity * sizeof(HOOK_GUARD_SLOT);
	PVOID Buffer = NULL;
	if (!VirtualMemory::AllocateNonPagedPool(&Buffer, AllocationSize, FALSE))
	{
		LogError("Failed to allocate HookGuard hash table (%lu bytes)", AllocationSize);
		return FALSE;
	}

	RtlZeroMemory(Buffer, AllocationSize);
	m_Slots = (PHOOK_GUARD_SLOT)Buffer;
	m_Capacity = Capacity;
	m_Mask = Capacity - 1;
	m_ActiveHookCount = 0;
	m_Stopping = FALSE;
	m_Initialized = TRUE;

	LogInformation("HookGuard initialized successfully: %lu cores -> %lu slots (%lu bytes)",
		(ULONG)LogicalProcessorsCount, m_Capacity, AllocationSize);

	return TRUE;
}

VOID
HookGuard::Destroy()
{
	if (!m_Initialized)
	{
		return;
	}

	m_Stopping = TRUE;

	if (m_Slots != NULL)
	{
		VirtualMemory::FreeNonPagedPool(m_Slots);
		m_Slots = NULL;
	}

	m_Capacity = 0;
	m_Mask = 0;
	m_ActiveHookCount = 0;
	m_Initialized = FALSE;

	LogInformation("HookGuard destroyed");
	return;
}

VOID
HookGuard::BeginQuiescence()
{
	m_Stopping = TRUE;
}

BOOLEAN
HookGuard::IsQuiescing()
{
	return m_Stopping;
}

VOID
HookGuard::WaitForDraining()
{
	m_Stopping = TRUE;

	LARGE_INTEGER Delay;
	Delay.QuadPart = -10000; // 1 ms

	// Drain in-flight hook threads with a safe timeout (up to 500 ms)
	for (ULONG Timeout = 0; Timeout < 500; Timeout++)
	{
		if (m_ActiveHookCount <= 0)
		{
			break;
		}
		KeDelayExecutionThread(KernelMode, FALSE, &Delay);
	}
}

BOOLEAN
HookGuard::IsInitialized()
{
	return m_Initialized;
}

LONG
HookGuard::GetActiveHookCount()
{
	return m_ActiveHookCount;
}

BOOLEAN
HookGuard::Enter(PVOID HookId, ULONG* OutSlot, ULONG_PTR MinSafeStack)
{
	if (OutSlot != NULL)
	{
		*OutSlot = HOOK_GUARD_INVALID_SLOT;
	}

	// Initialization check: If Guard is not initialized or destroyed, reject entry to bypass hook safely
	if (!m_Initialized || m_Slots == NULL)
	{
		return FALSE;
	}

	// Quiescence check: if unloading or stopped, reject entry immediately to prevent BSOD 0xCE
	if (m_Stopping)
	{
		return FALSE;
	}

	// Hardware remaining stack safety gate: bypass hook to prevent 0x1AA BSOD
	if (KeGetCurrentIrql() <= APC_LEVEL)
	{
		if (HookGuard::GetRemainingStackSize() < MinSafeStack)
		{
			return FALSE;
		}
	}

	PVOID CurrentThread = (PVOID)PsGetCurrentThread();
	if (CurrentThread == NULL)
	{
		return FALSE;
	}

	// Dual hash based on Thread pointer and HookId
	ULONG_PTR Hash = ((ULONG_PTR)CurrentThread >> 4) ^ ((ULONG_PTR)HookId >> 3);
	Hash ^= (Hash >> 16);
	ULONG StartIndex = (ULONG)(Hash & m_Mask);

	// Reentrancy check
	for (ULONG i = 0; i < HOOK_GUARD_PROBE_DEPTH; i++)
	{
		ULONG Slot = (StartIndex + i) & m_Mask;
		if (m_Slots[Slot].Thread == CurrentThread && m_Slots[Slot].HookId == HookId)
		{
			return FALSE;
		}
	}

	// Find empty slot and claim it with atomic CAS
	for (ULONG i = 0; i < HOOK_GUARD_PROBE_DEPTH; i++)
	{
		ULONG Slot = (StartIndex + i) & m_Mask;
		if (m_Slots[Slot].Thread == NULL)
		{
			if (InterlockedCompareExchangePointer(&m_Slots[Slot].Thread, CurrentThread, NULL) == NULL)
			{
				m_Slots[Slot].HookId = HookId;
				if (OutSlot != NULL)
				{
					*OutSlot = Slot;
				}
				InterlockedIncrement(&m_ActiveHookCount);
				return TRUE;
			}
		}
	}

	// All probe slots occupied
	return FALSE;
}

VOID
HookGuard::Leave(ULONG SlotIndex)
{
	if (!m_Initialized || m_Slots == NULL || SlotIndex >= m_Capacity)
	{
		return;
	}

	m_Slots[SlotIndex].HookId = NULL;
	InterlockedExchangePointer(&m_Slots[SlotIndex].Thread, NULL);
	InterlockedDecrement(&m_ActiveHookCount);
	return;
}
