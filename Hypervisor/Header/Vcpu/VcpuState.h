#pragma once
#include <ntifs.h>

typedef struct __VCPU_STATE
{
	ULONG_PTR VmmStack;
	ULONG_PTR VmxonRegionVirtualAddress;
	ULONG_PTR VmxonRegionPhysicalAddress;
	ULONG_PTR VmcsRegionVirtualAddress;
	ULONG_PTR VmcsRegionPhysicalAddress;
	ULONG_PTR MsrBitmapVirtualAddress;
	ULONG_PTR MsrBitmapPhysicalAddress;
	BOOLEAN VmxonAllocatedFailed;
	BOOLEAN VmcsAllocatedFailed;
	BOOLEAN MsrBitmapAllocatedFailed;
	BOOLEAN VmmStackAllocatedFailed;
	BOOLEAN VmxonStatus;
	BOOLEAN VmcsClearStatus;
	BOOLEAN VmcsLoadStatus;
	BOOLEAN VmcsLaunchStatus;
	ULONG PrimaryProcControls;
	ULONG PrimaryProcControlsWithMtf;
	PVOID CurrentActiveHook;
} VCPU_STATE, * PVCPU_STATE;