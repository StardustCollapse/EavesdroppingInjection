#pragma once
#include <ntifs.h>
#include "VmExit/VmExit.h"

#define VMCALL_STOP_HV 0x53544F505F4856ULL //STOP_HV

//Manage VmCall Handler
class VmCall
{
public:
	static BOOLEAN VmcallStopHv(PGUEST_CONTEXT Context);
};