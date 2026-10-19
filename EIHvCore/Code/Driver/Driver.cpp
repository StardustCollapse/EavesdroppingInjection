#include "Driver/Driver.h"
#include "Vmm/Vmm.h"
#include "Log/Log.h"

PDEVICE_OBJECT Driver::DeviceObject = NULL;

NTSTATUS
Driver::DriverEntry(
    PDRIVER_OBJECT  DriverObject,
    PUNICODE_STRING RegistryPath
) {
    UNREFERENCED_PARAMETER(RegistryPath);
    
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\EIHvCore");
    UNICODE_STRING DosDevicesName = RTL_CONSTANT_STRING(L"\\DosDevices\\EIControlCenter");

    status = IoCreateDevice(
        DriverObject,
        0,
        &DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        TRUE,
        &Driver::DeviceObject
    );

    if (!NT_SUCCESS(status))
    {
		LogError("Failed to create driver device. Status: 0x%X", status);
        return status;
    }

    LogInformation("Driver Device created successfully");

    //set up the dispatch routines for the driver.
    for (ULONG64 Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        DriverObject->MajorFunction[Index] = Driver::DriverUnsupported;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = Driver::DriverCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Driver::DriverClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Driver::DriverDeviceControl;
    DriverObject->DriverUnload = Driver::DriverUnload;

    status = IoCreateSymbolicLink(&DosDevicesName, &DeviceName);

    if (!NT_SUCCESS(status))
    {
		LogError("Failed to create symbolic link. Status: 0x%X", status);
        IoDeleteDevice(Driver::DeviceObject);
		return status;
    }
	LogInformation("Symbolic link created successfully");

    Driver::DeviceObject->Flags |= DO_BUFFERED_IO;
    Driver::DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

VOID
Driver::DriverUnload(
    PDRIVER_OBJECT DriverObject
) {
    UNREFERENCED_PARAMETER(DriverObject);

    Vmm::UnLoadVMM();

    UNICODE_STRING DosDevicesName = RTL_CONSTANT_STRING(L"\\DosDevices\\EIControlCenter");
    IoDeleteSymbolicLink(&DosDevicesName);

    if (Driver::DeviceObject)
    {
        IoDeleteDevice(Driver::DeviceObject);
        Driver::DeviceObject = NULL;
    }

	LogInformation("Driver unloaded successfully");
}