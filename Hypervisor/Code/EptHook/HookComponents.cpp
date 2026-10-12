#include "EptHook/HookComponents.h"
#include "EptHook/EptHook.h"
#include "EptHook/HookGuard.h"
#include "Vmm/Vmm.h"
#include "Util.h"
#include "Log/Log.h"

fnExAllocatePool2Hook HookCompoents::OriginalExAllocatePool2 = NULL;
fnNtCreateFile HookCompoents::OriginalNtCreateFile = NULL;
fnNtQuerySystemInformation HookCompoents::OriginalNtQuerySystemInformation = NULL;

VOID
HookCompoents::InitializeHook()
{
	HookGuard::Initialize(Vmm::VmmState.LogicalProcessorsCount);

	PVOID NtCreateFile = Util::GetSsdtFunctionAddress(0x55);
	if (NtCreateFile == NULL)
	{
		LogError("Can't Find NtCreateFile Function");
	}
	else
	{
		EptHook::Hook(NtCreateFile, NtCreateFileHook, (PVOID*)&OriginalNtCreateFile);
	}

	PVOID NtQuerySystemInformation = Util::GetSsdtFunctionAddress(0x36);
	if (NtQuerySystemInformation == NULL)
	{
		LogError("Can't Find NtQuerySystemInformation Function");
	}
	else
	{
		EptHook::PostHook(NtQuerySystemInformation, (PVOID)NtQuerySystemInformationPostHook);
	}

	EptHook::Hook(ExAllocatePool2, ExAllocatePool2Hook, (PVOID *)&OriginalExAllocatePool2);
	return;
}

VOID
HookCompoents::DestroyHook()
{
	HookGuard::Destroy();
}

PVOID
HookCompoents::ExAllocatePool2Hook(
	POOL_TYPE	PoolType,
	SIZE_T      NumberOfBytes,
	ULONG       Tag
)
{
	SCOPED_HOOK_GUARD(Guard);
	if (!Guard.IsAcquired())
	{
		return OriginalExAllocatePool2(PoolType, NumberOfBytes, Tag);
	}

	// Sample logging: 1 out of every 1024 allocations to eliminate DbgPrintEx bottleneck and lag
	static volatile LONG s_AllocCount = 0;
	LONG Count = InterlockedIncrement(&s_AllocCount);
	if ((Count & 0x3FF) == 1)
	{
		LogInformation("ExAllocatePool2 (Sampled #%ld): Tag = 0x%X, Bytes = %I64u, Type = %d", Count, Tag, (ULONG64)NumberOfBytes, PoolType);
	}
	return OriginalExAllocatePool2(PoolType, NumberOfBytes, Tag);
}

static VOID InspectNtCreateFileParameters(KPROCESSOR_MODE PreviousMode, PHANDLE FileHandle, POBJECT_ATTRIBUTES ObjectAttributes)
{
	if (PreviousMode == UserMode)
	{
		__try {
			if (FileHandle != NULL)
			{
				ProbeForRead(FileHandle, sizeof(HANDLE), sizeof(HANDLE));
			}

			if (ObjectAttributes != NULL)
			{
				ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), sizeof(ULONG_PTR));

				PUNICODE_STRING ObjectName = ObjectAttributes->ObjectName;
				if (ObjectName != NULL)
				{
					ProbeForRead(ObjectName, sizeof(UNICODE_STRING), sizeof(ULONG_PTR));

					if (ObjectName->Buffer != NULL && ObjectName->Length > 0)
					{
						ProbeForRead(ObjectName->Buffer, ObjectName->Length, sizeof(WCHAR));

						WCHAR LocalBuffer[260] = { 0 };
						ULONG CopyLength = ObjectName->Length < sizeof(LocalBuffer) - sizeof(WCHAR) ? ObjectName->Length : sizeof(LocalBuffer) - sizeof(WCHAR);
						RtlCopyMemory(LocalBuffer, ObjectName->Buffer, CopyLength);
						LocalBuffer[CopyLength / sizeof(WCHAR)] = L'\0';

						LogInformation("NtCreateFile [UserMode] called for : %ws", LocalBuffer);
					}
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogError("NtCreateFile user probe exception: 0x%X", GetExceptionCode());
		}
	}
	else
	{
		// KernelMode caller : pointers are in kernel space, DON'T Call ProbeForRead!!!
		__try {
			if (ObjectAttributes != NULL && ObjectAttributes->ObjectName != NULL &&
				ObjectAttributes->ObjectName->Buffer != NULL && ObjectAttributes->ObjectName->Length > 0)
			{
				LogInformation("NtCreateFile [KernelMode] called for : %wZ", ObjectAttributes->ObjectName);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogError("NtCreateFile kernel read exception: 0x%X", GetExceptionCode());
		}
	}
}

NTSTATUS
HookCompoents::NtCreateFileHook(
	PHANDLE            FileHandle,
	ACCESS_MASK        DesiredAccess,
	POBJECT_ATTRIBUTES ObjectAttributes,
	PIO_STATUS_BLOCK   IoStatusBlock,
	PLARGE_INTEGER     AllocationSize,
	ULONG              FileAttributes,
	ULONG              ShareAccess,
	ULONG              CreateDisposition,
	ULONG              CreateOptions,
	PVOID              EaBuffer,
	ULONG              EaLength
)
{
	SCOPED_HOOK_GUARD(Guard);
	if (!Guard.IsAcquired())
	{
		return OriginalNtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes,
			ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
	}

	KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
	InspectNtCreateFileParameters(PreviousMode, FileHandle, ObjectAttributes);

	return OriginalNtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes,
		ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

NTSTATUS
HookCompoents::NtQuerySystemInformationHook(
	SYSTEM_INFORMATION_CLASS SystemInformationClass,
	PVOID                    SystemInformation,
	ULONG                    SystemInformationLength,
	PULONG                   ReturnLength
)
{
	SCOPED_HOOK_GUARD(Guard);
	if (!Guard.IsAcquired() || (SystemInformationClass != SystemProcessInformation))
	{
		return OriginalNtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
	}
	
	return OriginalNtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
}

ULONG64
HookCompoents::NtQuerySystemInformationPostHook(PGUEST_CONTEXT Context, ULONG64 Result)
{
	SCOPED_HOOK_GUARD(Guard);
	if (!Guard.IsAcquired())
	{
		return Result;
	}

	NTSTATUS Status = (NTSTATUS)Result;
	if (NT_SUCCESS(Status) && Context != NULL)
	{
		SYSTEM_INFORMATION_CLASS InfoClass = (SYSTEM_INFORMATION_CLASS)Context->Rcx;
		if (InfoClass == SystemProcessInformation && Context->Rdx != 0)
		{
			*(PSYSTEM_PROCESS_INFORMATION)Context->Rdx = { 0 };
			static volatile LONG s_QueryCount = 0;
			LONG Count = InterlockedIncrement(&s_QueryCount);
			LogInformation("NtQuerySystemInformation [PostHook #%ld]: Status = 0x%08X, Buffer = %p, CallerRip = %p",
				Count, Status, (PVOID)Context->Rdx, (PVOID)Context->GuestRip);
		}
	}

	return Result;
}