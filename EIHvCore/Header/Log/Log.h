#pragma once
#include <ntifs.h>

//#define IsDeBugEnable TRUE

#if DBG
#define LogInformation(Format, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "EavesdroppingInjection> [Information | %s:%d] " Format " \n", __FUNCTION__, __LINE__, __VA_ARGS__)

#define LogWarning(Format, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "EavesdroppingInjection> [Warning | %s:%d] " Format " \n", __FUNCTION__, __LINE__, __VA_ARGS__)

#define LogError(Format, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "EavesdroppingInjection> [Error | %s:%d] " Format " \n", __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define LogInformation(Format, ...) ((VOID)0)

#define LogWarning(Format, ...) ((VOID)0)

#define LogError(Format, ...) ((VOID)0)
#endif


//Manage Log
class Log
{
public:
	//static VOID DebugPrintInformation(PCSTR Format, ...);
	//static VOID DebugPrintWarning(PCSTR Format, ...);
	//static VOID DebugPrintError(PCSTR Format, ...);
};