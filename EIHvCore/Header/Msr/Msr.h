#pragma once
#include <ntifs.h>
#include "MsrControlField.h"

//Adjust Msr
class Msr
{
public:
	static MSR_IA32_FEATURE_CONTROL IA32FeatureControl;
	static MSR_IA32_VMX_BASIC IA32VmxBasic;

	static BOOLEAN SetMsrBitmap(ULONG_PTR MsrBitmap, ULONG64 Msr, BOOLEAN Read, BOOLEAN Write);
};