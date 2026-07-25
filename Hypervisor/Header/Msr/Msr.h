#pragma once
#include <ntifs.h>
#include "MsrControlField.h"

//Adjust Msr
class Msr
{
public:
	static MSR_IA32_FEATURE_CONTROL IA32FeatureControl;
	static MSR_IA32_VMX_BASIC IA32VmxBasic;
};