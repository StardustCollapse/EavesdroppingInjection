#pragma once
#include <ntifs.h>
#include <intrin.h>
#include "VmExit/VmExit.h"

#define EPT_PML1E_COUNT 512
#define EPT_PML2E_COUNT 512
#define EPT_PML3E_COUNT 512
#define EPT_PML4E_COUNT 1

#define EPT_MAPPED_SIZE (4096ULL * EPT_PML1E_COUNT * EPT_PML2E_COUNT * EPT_PML3E_COUNT * EPT_PML4E_COUNT)

#define EPT_PHYSICAL_ADDRESS(PageFrameNumber) (((ULONG64)(PageFrameNumber)) << 12)
#define EPT_PAGE_FRAME_NUMBER(PhysicalAddress) (((ULONG64)(PhysicalAddress)) >> 12)
#define EPT_PAGE_OFFSET(PhysicalAddress) ((ULONG)(((ULONG64)(PhysicalAddress)) & 0xFFFULL))

#define EPT_PML1_ADDRESS_OFFSET(Index) (((ULONG64)(Index)) << 12)
#define EPT_PML2_ADDRESS_OFFSET(Index) (((ULONG64)(Index)) << 21)
#define EPT_PML3_ADDRESS_OFFSET(Index) (((ULONG64)(Index)) << 30)
#define EPT_PML4_ADDRESS_OFFSET(Index) (((ULONG64)(Index)) << 39)

#define EPT_PML1_PAGE_FRAME_NUMBER(PhysicalAddress) (((ULONG64)(PhysicalAddress)) >> 12)
#define EPT_PML2_PAGE_FRAME_NUMBER(PhysicalAddress) (((ULONG64)(PhysicalAddress)) >> 21)
#define EPT_PML3_PAGE_FRAME_NUMBER(PhysicalAddress) (((ULONG64)(PhysicalAddress)) >> 30)
#define EPT_PML4_PAGE_FRAME_NUMBER(PhysicalAddress) (((ULONG64)(PhysicalAddress)) >> 39)

#define EPT_PHYSICAL_ADDRESS_ALIGNED_2MB(PhysicalAddress) (((ULONG64)(PhysicalAddress)) & ~0x1FFFFFULL)

#define EPT_PML1_INDEX(PhysicalAddress) (EPT_PML1_PAGE_FRAME_NUMBER(PhysicalAddress) & 0x1FFULL)
#define EPT_PML2_INDEX(PhysicalAddress) (EPT_PML2_PAGE_FRAME_NUMBER(PhysicalAddress) & 0x1FFULL)
#define EPT_PML3_INDEX(PhysicalAddress) (EPT_PML3_PAGE_FRAME_NUMBER(PhysicalAddress) & 0x1FFULL)
#define EPT_PML4_INDEX(PhysicalAddress) (EPT_PML4_PAGE_FRAME_NUMBER(PhysicalAddress) & 0x1FFULL)

typedef union _EPTP
{
	ULONG64 Value;
	struct
	{
		ULONG64 MemoryType : 3;               // Bits 2:0, 0 = UC, 6 = WB
		ULONG64 PageWalkLength : 3;           // Bits 5:3, Page walk length minus 1 (3 = 4 levels)
		ULONG64 EnableAccessedDirtyFlags : 1; // Bit 6
		ULONG64 Reserved1 : 5;                // Bits 11:7 : Reserved (must be 0)
		ULONG64 Pml4PhysicalAddress : 40;     // Bits 51:12
		ULONG64 Reserved2 : 12;               // Bits 63:52 : Reserved (must be 0)
	} Fields;
} EPTP, * PEPTP;

typedef union _EPT_PML4E
{
	ULONG64 Value;
	struct
	{
		ULONG64 Read : 1;                     // Bit 0
		ULONG64 Write : 1;                    // Bit 1
		ULONG64 Execute : 1;                  // Bit 2
		ULONG64 Reserved1 : 5;                // Bits 7:3 : Reserved (must be 0)
		ULONG64 Accessed : 1;                 // Bit 8
		ULONG64 Ignored1 : 1;                 // Bit 9
		ULONG64 ExecuteForUserMode : 1;       // Bit 10
		ULONG64 Ignored2 : 1;                 // Bit 11
		ULONG64 PdptePhysicalAddress : 40;    // Bits 51:12
		ULONG64 Ignored3 : 12;                // Bits 63:52
	} Fields;
} EPT_PML4E, * PEPT_PML4E;

typedef union _EPT_PDPTE
{
	ULONG64 Value;
	struct
	{
		ULONG64 Read : 1;                     // Bit 0
		ULONG64 Write : 1;                    // Bit 1
		ULONG64 Execute : 1;                  // Bit 2
		ULONG64 Reserved1 : 4;                // Bits 6:3 : Reserved (must be 0)
		ULONG64 LargePage : 1;                // Bit 7
		ULONG64 Accessed : 1;                 // Bit 8
		ULONG64 Ignored1 : 1;                 // Bit 9
		ULONG64 ExecuteForUserMode : 1;       // Bit 10
		ULONG64 Ignored2 : 1;                 // Bit 11
		ULONG64 PdePhysicalAddress : 40;      // Bits 51:12
		ULONG64 Ignored3 : 12;                // Bits 63:52
	} Fields;
} EPT_PDPTE, * PEPT_PDPTE;

typedef union _EPT_PDE_2MB
{
	ULONG64 Value;
	struct
	{
		ULONG64 Read : 1;                     // Bit 0
		ULONG64 Write : 1;                    // Bit 1
		ULONG64 Execute : 1;                  // Bit 2
		ULONG64 MemoryType : 3;               // Bits 5:3
		ULONG64 IgnorePat : 1;                // Bit 6
		ULONG64 LargePage : 1;                // Bit 7 : Must be 1 for 2MB page
		ULONG64 Accessed : 1;                 // Bit 8
		ULONG64 Dirty : 1;                    // Bit 9
		ULONG64 ExecuteForUserMode : 1;       // Bit 10
		ULONG64 Ignored1 : 10;                // Bits 20:11
		ULONG64 PageFrameNumber : 31;         // Bits 51:21
		ULONG64 Ignored2 : 11;                // Bits 62:52
		ULONG64 SuppressVE : 1;               // Bit 63
	} Fields;
} EPT_PDE_2MB, * PEPT_PDE_2MB;

typedef union _EPT_PDE
{
	ULONG64 Value;
	struct
	{
		ULONG64 Read : 1;                     // Bit 0
		ULONG64 Write : 1;                    // Bit 1
		ULONG64 Execute : 1;                  // Bit 2
		ULONG64 Reserved1 : 4;                // Bits 6:3
		ULONG64 LargePage : 1;                // Bit 7
		ULONG64 Accessed : 1;                 // Bit 8
		ULONG64 Ignored1 : 1;                 // Bit 9
		ULONG64 ExecuteForUserMode : 1;       // Bit 10
		ULONG64 Ignored2 : 1;                 // Bit 11
		ULONG64 PtePhysicalAddress : 40;      // Bits 51:12
		ULONG64 Ignored3 : 12;                // Bits 63:52
	} Fields;
} EPT_PDE, * PEPT_PDE;

typedef union _EPT_PTE
{
	ULONG64 Value;
	struct
	{
		ULONG64 Read : 1;                     // Bit 0
		ULONG64 Write : 1;                    // Bit 1
		ULONG64 Execute : 1;                  // Bit 2
		ULONG64 MemoryType : 3;               // Bits 5:3
		ULONG64 IgnorePat : 1;                // Bit 6
		ULONG64 Ignored1 : 1;                 // Bit 7
		ULONG64 Accessed : 1;                 // Bit 8
		ULONG64 Dirty : 1;                    // Bit 9
		ULONG64 ExecuteForUserMode : 1;       // Bit 10
		ULONG64 Ignored2 : 1;                 // Bit 11
		ULONG64 PageFrameNumber : 40;         // Bits 51:12
		ULONG64 Ignored3 : 11;                // Bits 62:52
		ULONG64 SuppressVE : 1;               // Bit 63
	} Fields;
} EPT_PTE, * PEPT_PTE;

typedef struct _EPT_PAGE_DIRECTORY
{
	EPT_PDE_2MB Entries[512];
} EPT_PAGE_DIRECTORY, * PEPT_PAGE_DIRECTORY;

typedef struct _INVEPT_DESCRIPTOR
{
	ULONG64 EptPointer;
	ULONG64 Reserved;
} INVEPT_DESCRIPTOR, * PINVEPT_DESCRIPTOR;

enum INVEPT_TYPE
{
	INVEPT_SINGLE_CONTEXT = 1,
	INVEPT_ALL_CONTEXTS = 2
};

typedef union _EPT_VIOLATION_QUALIFICATION
{
	ULONG64 Value;
	struct
	{
		ULONG64 ReadAccess : 1;                 // Bit 0
		ULONG64 WriteAccess : 1;                // Bit 1
		ULONG64 ExecuteAccess : 1;              // Bit 2
		ULONG64 EptReadable : 1;                // Bit 3
		ULONG64 EptWriteable : 1;               // Bit 4
		ULONG64 EptExecutable : 1;              // Bit 5
		ULONG64 EptExecutableForUser : 1;       // Bit 6
		ULONG64 GuestLinearAddressValid : 1;    // Bit 7
		ULONG64 CausedByTranslation : 1;        // Bit 8
		ULONG64 UserModeLinearAddress : 1;      // Bit 9
		ULONG64 ReadWriteLinearAddress : 1;     // Bit 10
		ULONG64 ExecuteDisableLinearAddress : 1;// Bit 11
		ULONG64 NmiUnblockingDueToIret : 1;     // Bit 12
		ULONG64 Reserved : 51;
	} Fields;
} EPT_VIOLATION_QUALIFICATION, * PEPT_VIOLATION_QUALIFICATION;

typedef struct _SPLIT_PAGE_NODE
{
	struct _SPLIT_PAGE_NODE* Next;
	PVOID PageTableVirtualAddress;
} SPLIT_PAGE_NODE, * PSPLIT_PAGE_NODE;

typedef struct _EPT_STATE
{
	PEPT_PML4E Pml4Table;                     // 4KB-aligned virtual address
	ULONG64 Pml4PhysicalAddress;

	PEPT_PDPTE PdptTable;                     // 4KB-aligned virtual address
	ULONG64 PdptPhysicalAddress;

	PEPT_PAGE_DIRECTORY PdTables;             // 512 * 4KB = 2MB virtual address
	ULONG64 PdTablesPhysicalAddress;

	EPTP EptPointer;                          // EPTP for VMCS EPT_Pointer_Full

	PSPLIT_PAGE_NODE SplitPageListHead;
} EPT_STATE, * PEPT_STATE;

EXTERN_C UCHAR AsmInvept(ULONG Type, PINVEPT_DESCRIPTOR Descriptor);

class Ept
{
public:
	static EPT_STATE EptState;

	static BOOLEAN IsEptSupported();
	static BOOLEAN InitializeEpt();
	static VOID DestroyEpt();
	static EPTP GetEptPointer();

	static ULONG_PTR InveptAllContexts(ULONG_PTR Arguments);
	static ULONG_PTR InveptSingleContext(ULONG_PTR EptPointer = 0);

	static BOOLEAN HandleEptViolation(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress, ULONG64 ExitQualification);
	static BOOLEAN HandleEptMisconfiguration(PGUEST_CONTEXT Context, ULONG64 GuestPhysicalAddress);

	// Use for EPT Hooking:
	static PEPT_PTE SplitLargePageTo4Kb(ULONG64 PhysicalAddress);
	static PEPT_PTE GetPte(ULONG64 PhysicalAddress);
};