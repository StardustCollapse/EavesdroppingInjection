#pragma once
#include <ntifs.h>

extern "C" {
_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
VOID
IoGetStackLimits(
	_Out_ PULONG_PTR LowLimit,
	_Out_ PULONG_PTR HighLimit
);
}

// Preserve 8KB stack size to prevent BSOD: 0x1AA (EXCEPTION_ON_INVALID_STACK)
#define HOOK_GUARD_DEFAULT_MIN_STACK 0x2000
#define HOOK_GUARD_INVALID_SLOT 0xFFFFFFFF

// Hash bucket slot for lockless per-thread and per-hook tracking
typedef struct _HOOK_GUARD_SLOT
{
	volatile PVOID Thread;
	volatile PVOID HookId;
} HOOK_GUARD_SLOT, *PHOOK_GUARD_SLOT;

class HookGuard
{
public:
	static BOOLEAN Initialize(ULONG64 LogicalProcessorsCount);
	static VOID Destroy();

	// Quiescence and Drain control for safe driver unloading, prevents BSOD 0xCE
	static VOID BeginQuiescence();
	static VOID WaitForDraining();
	static BOOLEAN IsQuiescing();

	static BOOLEAN Enter(PVOID HookId, ULONG* OutSlot, ULONG_PTR MinSafeStack = HOOK_GUARD_DEFAULT_MIN_STACK);
	static VOID Leave(ULONG SlotIndex);

	// Query remaining kernel stack space of current thread
	FORCEINLINE static ULONG_PTR GetRemainingStackSize()
	{
		ULONG_PTR Top = 0;
		ULONG_PTR Bottom = 0;
		IoGetStackLimits(&Bottom, &Top);
		return ((ULONG_PTR)(&Top) - Bottom);
	}

	static BOOLEAN IsInitialized();
	static LONG GetActiveHookCount();

private:
	static PHOOK_GUARD_SLOT m_Slots;
	static ULONG m_Capacity;
	static ULONG m_Mask;
	static BOOLEAN m_Initialized;
	static volatile LONG m_ActiveHookCount;
	static volatile BOOLEAN m_Stopping;
};

class ScopedHookGuard
{
public:
	FORCEINLINE ScopedHookGuard(PVOID HookId = NULL, ULONG_PTR MinSafeStack = HOOK_GUARD_DEFAULT_MIN_STACK)
		: m_SlotIndex(HOOK_GUARD_INVALID_SLOT), m_Acquired(FALSE)
	{
		ULONG ClaimedSlot = HOOK_GUARD_INVALID_SLOT;
		if (HookGuard::Enter(HookId, &ClaimedSlot, MinSafeStack))
		{
			m_SlotIndex = ClaimedSlot;
			m_Acquired = TRUE;
		}
	}

	FORCEINLINE ~ScopedHookGuard()
	{
		if (m_Acquired && m_SlotIndex != HOOK_GUARD_INVALID_SLOT)
		{
			HookGuard::Leave(m_SlotIndex);
		}
	}

	FORCEINLINE BOOLEAN IsAcquired() const
	{
		return m_Acquired;
	}

	FORCEINLINE VOID Release()
	{
		if (m_Acquired && m_SlotIndex != HOOK_GUARD_INVALID_SLOT)
		{
			HookGuard::Leave(m_SlotIndex);
			m_Acquired = FALSE;
			m_SlotIndex = HOOK_GUARD_INVALID_SLOT;
		}
	}

private:
	ULONG m_SlotIndex;
	BOOLEAN m_Acquired;
};

// Uses __FUNCTION__ as the unique HookId
#define SCOPED_HOOK_GUARD(GuardVar) ScopedHookGuard GuardVar((PVOID)__FUNCTION__)
#define SCOPED_HOOK_GUARD_CUSTOM(GuardVar, HookId, MinStack) ScopedHookGuard GuardVar((PVOID)(HookId), (MinStack))
