#pragma once
#include <ntifs.h>
#include <intrin.h>

typedef union __CPUID_ECX
{
	struct __01H
	{
		ULONG SSE3 : 1;
		ULONG PCLMULQDQ : 1;
		ULONG DTES64 : 1;
		ULONG MONITOR : 1;
		ULONG DS_CPL : 1;
		ULONG VMX : 1;
		ULONG SMX : 1;
		ULONG EIST : 1;
		ULONG TM2 : 1;
		ULONG SSSE3 : 1;
		ULONG CNXT_ID : 1;
		ULONG SDBG : 1;
		ULONG FMA : 1;
		ULONG CMPXCHG16B : 1;
		ULONG xTPR_UPDATE_CONTROL : 1;
		ULONG PDCM : 1;
		ULONG Reserved : 1;
		ULONG PCID : 1;
		ULONG DCA : 1;
		ULONG SSE4_1 : 1;
		ULONG SSE4_2 : 1;
		ULONG x2APIC : 1;
		ULONG MOVBE : 1;
		ULONG POPCNT : 1;
		ULONG TSC_Deadline : 1;
		ULONG AESNI : 1;
		ULONG XSAVE : 1;
		ULONG OSXSAVE : 1;
		ULONG AVX : 1;
		ULONG F16C : 1;
		ULONG RDRAND : 1;
		ULONG IsHypervisorEnable : 1;
	} ECX_01H;
	ULONG info;
} CPUID_ECX, *PCPUID_ECX;

typedef struct __CPUID
{
	ULONG eax;
	ULONG ebx;
	CPUID_ECX ecx;
	ULONG edx;
} CPUID, *PCPUID;