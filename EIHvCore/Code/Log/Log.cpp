//#include "Log/Log.h"
//
//VOID
//Log::DebugPrintInformation(PCSTR Format, ...)
//{
//	va_list args;
//	_crt_va_start(args, Format);
//	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, Format, args);
//	_crt_va_end(args);
//}
//
//VOID
//Log::DebugPrintWarning(PCSTR Format, ...)
//{
//	va_list args;
//	_crt_va_start(args, Format);
//	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, Format, args);
//	_crt_va_end(args);
//}
//
//VOID
//Log::DebugPrintError(PCSTR Format, ...)
//{
//	va_list args;
//	_crt_va_start(args, Format);
//	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, Format, args);
//	_crt_va_end(args);
//}