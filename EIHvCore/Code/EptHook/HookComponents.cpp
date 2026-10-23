#include "EptHook/HookComponents.h"
#include "EptHook/EptHook.h"
#include "EptHook/HookGuard.h"
#include "Vmm/Vmm.h"
#include "Util.h"
#include "Log/Log.h"


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
		EPT_FILTER_HOOK(NtCreateFile, NtCreateFilePreHook);
	}

	PVOID NtQuerySystemInformation = Util::GetSsdtFunctionAddress(0x36);
	if (NtQuerySystemInformation == NULL)
	{
		LogError("Can't Find NtQuerySystemInformation Function");
	}
	else
	{
		EPT_RETURN_HOOK(NtQuerySystemInformation, NtQuerySystemInformationPostHook);
	}

	EPT_ENTRY_HOOK(ExAllocatePool2, ExAllocatePool2PreHook);
	return;
}

VOID
HookCompoents::DestroyHook()
{
	HookGuard::Destroy();
}

VOID
HookCompoents::ExAllocatePool2PreHook(PGUEST_CONTEXT Context)
{
	POOL_TYPE PoolType      = (POOL_TYPE)Context->Rcx;
	SIZE_T    NumberOfBytes = (SIZE_T)Context->Rdx;
	ULONG     Tag           = (ULONG)Context->R8;
	UNREFERENCED_PARAMETER(PoolType);
	UNREFERENCED_PARAMETER(NumberOfBytes);
	UNREFERENCED_PARAMETER(Tag);

	static volatile LONG s_AllocCount = 0;
	LONG Count = InterlockedIncrement(&s_AllocCount);
	if ((Count & 0x3FF) == 1)
	{
		LogInformation("ExAllocatePool2 (Sampled #%ld): Tag = 0x%X, Bytes = %llu, Type = %d", Count, Tag, (ULONG64)NumberOfBytes, PoolType);
	}
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

BOOLEAN
HookCompoents::NtCreateFilePreHook(PGUEST_CONTEXT Context)
{
	PHANDLE            FileHandle       = (PHANDLE)Context->Rcx;
	POBJECT_ATTRIBUTES ObjectAttributes = (POBJECT_ATTRIBUTES)Context->R8;

	KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
	InspectNtCreateFileParameters(PreviousMode, FileHandle, ObjectAttributes);

	// Return FALSE: continue normal execution (do not skip actual NtCreateFile)
	return FALSE;
}
ULONG64
HookCompoents::NtQuerySystemInformationPostHook(PGUEST_CONTEXT Context, ULONG64 Result)
{

	NTSTATUS Status = (NTSTATUS)Result;
	if (NT_SUCCESS(Status) && Context != NULL)
	{
		SYSTEM_INFORMATION_CLASS InfoClass = (SYSTEM_INFORMATION_CLASS)Context->Rcx;
		if (InfoClass == SystemProcessInformation && Context->Rdx != 0)
		{
			__try
			{
				// Sample logging of queried process information (do not zero buffer to prevent breaking caller/SMAP fault)
				static volatile LONG s_QueryCount = 0;
				LONG Count = InterlockedIncrement(&s_QueryCount);
				UNREFERENCED_PARAMETER(Count);
				LogInformation("NtQuerySystemInformation [PostHook #%ld]: Status = 0x%08x, Buffer = %p, CallerRip = %p",
					Count, Status, (PVOID)Context->Rdx, (PVOID)Context->GuestRip);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogError("NtQuerySystemInformationPostHook: Exception 0x%x accessing buffer %p",
					GetExceptionCode(), (PVOID)Context->Rdx);
			}
		}
	}

	return Result;
}
