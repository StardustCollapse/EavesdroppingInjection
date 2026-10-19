#include "Driver/Driver.h"

EXTERN_C NTSTATUS
DriverEntry(
	PDRIVER_OBJECT  DriverObject,
	PUNICODE_STRING RegistryPath
) {
	return Driver::DriverEntry(DriverObject, RegistryPath);
}

EXTERN_C VOID
DriverUnload(
	PDRIVER_OBJECT DriverObject
) {
	return Driver::DriverUnload(DriverObject);
}