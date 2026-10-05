#pragma once
#include <ntifs.h>
#include "Vmcs/VmcsField.h"

#define GuestSegmentSelectorTransform(SegmentRegister) (Guest_State_Fields::Guest_ES_Selector + (SegmentRegister) * 2)
#define GuestSegmentLimitTransform(SegmentRegister) (Guest_State_Fields::Guest_ES_Limit + (SegmentRegister) * 2)
#define GuestSegmentAccessRightsTransform(SegmentRegister) (Guest_State_Fields::Guest_ES_Access_Rights + (SegmentRegister) * 2)
#define GuestSegmentBaseTransform(SegmentRegister) (Guest_State_Fields::Guest_ES_Base + (SegmentRegister) * 2)

enum SegmentRegisterIndex
{
	ES = 0,
	CS = 1,
	SS = 2,
	DS = 3,
	FS = 4,
	GS = 5,
	LDTR = 6,
	TR = 7
};

typedef struct __SEGMENT_INFORMATION
{
	ULONG64 Base;
	ULONG Limit;
	ULONG AccessRights;
	USHORT Selector;
}SEGMENT_INFORMATION, *PSEGMENT_INFORMATION;

typedef union __SEGMENT_SELECTOR
{
	struct __SELECTOR
	{
		USHORT RPL : 2;
		USHORT TI : 1;	//0 = GDT, 1 = LDT
		USHORT Index : 13;
	} Bits;
	USHORT Value;
}SEGMENT_SELECTOR, * PSEGMENT_SELECTOR;

typedef union __SEGMENT_DESCRIPTORS_64BIT
{
	struct __DESCRIPTOR
	{
		USHORT Limit1;
		USHORT Base1;
		UCHAR Base2;
		union __AccessRights1
		{
			struct __AccessRights_Bits
			{
				UCHAR Type : 4;
				UCHAR S : 1;
				UCHAR DPL : 2;
				UCHAR P : 1;
			} Bits;
			UCHAR Value;
		} AccessRights1;
		union __Limit2_Access2
		{
			struct __Limit_Access_Bits
			{
				UCHAR Limit : 4;

				UCHAR AVL : 1;
				UCHAR L : 1;
				UCHAR D_B : 1;
				UCHAR G : 1;
			} Bits;
			struct __Limit_Access
			{
				UCHAR Limit : 4;
				UCHAR AccessRights : 4;
			} Value;
		} Limit2AccessRights2;
		UCHAR Base3;
		ULONG Base4;
		ULONG Reserved1 : 8;
		ULONG Zero : 5;
		ULONG Reserved2 : 19;
	} Descriptor;
	struct __OctaPart
	{
		ULONG64 LowPart;
		ULONG64 HighPart;
	} OctaPart;
}SEGMENT_DESCRIPTOR, * PSEGMENT_DESCRIPTORS;