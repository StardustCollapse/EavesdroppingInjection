#pragma once
#include <ntifs.h>
#include "Ioctl.h"


//Manage Driver and Dispatch
class Driver
{
public:
	//Driver
	static NTSTATUS DriverEntry(PDRIVER_OBJECT  DriverObject, PUNICODE_STRING RegistryPath);
	static VOID DriverUnload(PDRIVER_OBJECT DriverObject);

private:
	static PDEVICE_OBJECT DeviceObject;

	//Dispatch
	static NTSTATUS DriverCreate(PDEVICE_OBJECT DeviceObjectParameter, PIRP Irp);
	static NTSTATUS DriverClose(PDEVICE_OBJECT DeviceObjectParameter, PIRP Irp);
	static NTSTATUS DriverDeviceControl(PDEVICE_OBJECT DeviceObjectParameter, PIRP Irp);
	static NTSTATUS DriverUnsupported(PDEVICE_OBJECT DeviceObjectParameter, PIRP Irp);
};
