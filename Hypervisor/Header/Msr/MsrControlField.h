#pragma once
#include <ntifs.h>
#include "util.h"

enum MSRs : ULONG
{
	IA32_FEATURE_CONTROL = 0x3A,
	IA32_DEBUGCTL = 0x1D9,

	IA32_LSTAR = 0xC0000082,

	IA32_SYSENTER_CS = 0x174,
	IA32_SYSENTER_ESP = 0x175,
	IA32_SYSENTER_EIP = 0x176,

	IA32_FS_BASE = 0xC0000100,
	IA32_GS_BASE = 0xC0000101,

	IA32_VMX_BASIC = 0x480,
	IA32_VMX_PINBASED_CTLS = 0x481,
	IA32_VMX_PROCBASED_CTLS = 0x482,
	IA32_VMX_EXIT_CTLS = 0x483,
	IA32_VMX_ENTRY_CTLS = 0x484,
	IA32_VMX_MISC = 0x485,
	IA32_VMX_CR0_FIXED0 = 0x486,
	IA32_VMX_CR0_FIXED1 = 0x487,
	IA32_VMX_CR4_FIXED0 = 0x488,
	IA32_VMX_CR4_FIXED1 = 0x489,
	IA32_VMX_VMCS_ENUM = 0x48A,
	IA32_VMX_PROCBASED_CTLS2 = 0x48B,
	IA32_VMX_EPT_VPID_CAP = 0x48C,
	IA32_VMX_TRUE_PINBASED_CTLS = 0x48D,
	IA32_VMX_TRUE_PROCBASED_CTLS = 0x48E,
	IA32_VMX_TRUE_EXIT_CTLS = 0x48F,
	IA32_VMX_TRUE_ENTRY_CTLS = 0x490,
	IA32_VMX_VMFUNC = 0x491,
	IA32_VMX_PROCBASED_CTLS3 = 0x492,
	IA32_VMX_EXIT_CTLS2 = 0x493
};

typedef union  __IA32_FEATURE_CONTROL
{
	struct __MSR
	{
		ULONG64 Lockbit : 1;
		ULONG64 EnableVmxonInSMX : 1;
		ULONG64 EnableVmxonOutsideSMX : 1;
		ULONG64 Reserved1 : 5;
		ULONG64 EnableSenterLocalFunction : 7;
		ULONG64 EnableSenterGlobal : 1;
		ULONG64 Reserved2 : 1;
		ULONG64 EnableSgxLaunchControl : 1;
		ULONG64 EnableSgxGlobal : 1;
		ULONG64 Reserved3 : 1;
		ULONG64 OnLMCE : 1;
		ULONG64 Reserved4 : 43;
	}Fields;
	ULONG64 MSR;
} MSR_IA32_FEATURE_CONTROL, * PMSR_IA32_FEATURE_CONTROL;

typedef union __IA32_VMX_BASIC_MSR
{
	struct __MSR
	{
		ULONG64 RevisionIdentifier : 31;
		ULONG64 Reserved1 : 1;
		ULONG64 RegionSize : 12;
		ULONG64 RegionClear : 1;
		ULONG64 Reserved2 : 3;
		ULONG64 SupportedIA64 : 1;
		ULONG64 SupportedDualMoniter : 1;
		ULONG64 MemoryType : 4;
		ULONG64 VmExitReport : 1;
		ULONG64 VmxCapabilityHint : 1;
		ULONG64 Reserved3 : 8;
	} Fields;
	ULONG64 MSR;
} MSR_IA32_VMX_BASIC, * PMSR_IA32_VMX_BASIC;

enum VM_Execution_Controls : ULONG
{
	// Pin-Based VM-Execution Controls
	PIN_BASED_VM_EXECUTION_CONTROLS_EXTERNAL_INTERRUPT            = Util::Bit(0),
	PIN_BASED_VM_EXECUTION_CONTROLS_NMI_EXITING                   = Util::Bit(3),
	PIN_BASED_VM_EXECUTION_CONTROLS_VIRTUAL_NMIS                  = Util::Bit(5),
	PIN_BASED_VM_EXECUTION_CONTROLS_ACTIVATE_VMX_PREEMPTION_TIMER = Util::Bit(6),
	PIN_BASED_VM_EXECUTION_CONTROLS_PROCESS_POSTED_INTERRUPTS     = Util::Bit(7),

	// Primary Processor-Based VM-Execution Controls
	CPU_BASED_INTERRUPT_WINDOW_EXITING          = Util::Bit(2),
	CPU_BASED_USE_TSC_OFFSETTING                = Util::Bit(3),
	CPU_BASED_HLT_EXITING                       = Util::Bit(7),
	CPU_BASED_INVLPG_EXITING                    = Util::Bit(9),
	CPU_BASED_MWAIT_EXITING                     = Util::Bit(10),
	CPU_BASED_RDPMC_EXITING                     = Util::Bit(11),
	CPU_BASED_RDTSC_EXITING                     = Util::Bit(12),
	CPU_BASED_CR3_LOAD_EXITING                  = Util::Bit(15),
	CPU_BASED_CR3_STORE_EXITING                 = Util::Bit(16),
	CPU_BASED_CR8_LOAD_EXITING                  = Util::Bit(19),
	CPU_BASED_CR8_STORE_EXITING                 = Util::Bit(20),
	CPU_BASED_USE_TPR_SHADOW                    = Util::Bit(21),
	CPU_BASED_NMI_WINDOW_EXITING                = Util::Bit(22),
	CPU_BASED_MOV_DR_EXITING                    = Util::Bit(23),
	CPU_BASED_UNCONDITIONAL_IO_EXITING          = Util::Bit(24),
	CPU_BASED_USE_IO_BITMAPS                    = Util::Bit(25),
	CPU_BASED_MONITOR_TRAP_FLAG                 = Util::Bit(27),
	CPU_BASED_USE_MSR_BITMAPS                   = Util::Bit(28),
	CPU_BASED_MONITOR_EXITING                   = Util::Bit(29),
	CPU_BASED_PAUSE_EXITING                     = Util::Bit(30),
	CPU_BASED_ACTIVATE_SECONDARY_CONTROLS       = Util::Bit(31),

	// Secondary Processor-Based VM-Execution Controls
	SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES      = Util::Bit(0),
	SECONDARY_EXEC_ENABLE_EPT                   = Util::Bit(1),
	SECONDARY_EXEC_DESCRIPTOR_TABLE_EXITING     = Util::Bit(2),
	SECONDARY_EXEC_ENABLE_RDTSCP                = Util::Bit(3),
	SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE       = Util::Bit(4),
	SECONDARY_EXEC_ENABLE_VPID                  = Util::Bit(5),
	SECONDARY_EXEC_WBINVD_EXITING               = Util::Bit(6),
	SECONDARY_EXEC_UNRESTRICTED_GUEST           = Util::Bit(7),
	SECONDARY_EXEC_APIC_REGISTER_VIRT           = Util::Bit(8),
	SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY        = Util::Bit(9),
	SECONDARY_EXEC_PAUSE_LOOP_EXITING           = Util::Bit(10),
	SECONDARY_EXEC_RDRAND_EXITING               = Util::Bit(11),
	SECONDARY_EXEC_ENABLE_INVPCID               = Util::Bit(12),
	SECONDARY_EXEC_ENABLE_VMFUNC                = Util::Bit(13),
	SECONDARY_EXEC_SHADOW_VMCS                  = Util::Bit(14),
	SECONDARY_EXEC_ENCLS_EXITING                = Util::Bit(15),
	SECONDARY_EXEC_RDSEED_EXITING               = Util::Bit(16),
	SECONDARY_EXEC_ENABLE_PML                   = Util::Bit(17),
	SECONDARY_EXEC_EPT_VIOLATION_VE             = Util::Bit(18),
	SECONDARY_EXEC_CONCEAL_VMX_FROM_PT          = Util::Bit(19),
	SECONDARY_EXEC_XSAVES                       = Util::Bit(20)
};

enum VM_Exit_Controls : ULONG
{
	VM_EXIT_SAVE_DEBUG_CONTROLS                 = Util::Bit(2),
	VM_EXIT_HOST_ADDR_SPACE_SIZE                = Util::Bit(9),
	VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL          = Util::Bit(12),
	VM_EXIT_ACK_INTR_ON_EXIT                    = Util::Bit(15),
	VM_EXIT_SAVE_IA32_PAT                       = Util::Bit(18),
	VM_EXIT_LOAD_IA32_PAT                       = Util::Bit(19),
	VM_EXIT_SAVE_IA32_EFER                      = Util::Bit(20),
	VM_EXIT_LOAD_IA32_EFER                      = Util::Bit(21),
	VM_EXIT_SAVE_VMX_PREEMPTION_TIMER           = Util::Bit(22)
};

enum VM_Entry_Controls : ULONG
{
	VM_ENTRY_LOAD_DEBUG_CONTROLS                = Util::Bit(2),
	VM_ENTRY_IA32E_MODE                         = Util::Bit(9),
	VM_ENTRY_SMM                                = Util::Bit(10),
	VM_ENTRY_DEACT_DUAL_MONITOR                 = Util::Bit(11),
	VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL         = Util::Bit(13),
	VM_ENTRY_LOAD_IA32_PAT                      = Util::Bit(14),
	VM_ENTRY_LOAD_IA32_EFER                     = Util::Bit(15),
	VM_ENTRY_LOAD_IA32_BNDCFGS                  = Util::Bit(16)
};