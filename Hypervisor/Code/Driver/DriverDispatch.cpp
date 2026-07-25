#include "Driver/Driver.h"
#include "Vmm/Vmm.h"

NTSTATUS
Driver::DriverCreate(
    PDEVICE_OBJECT DeviceObjectParameter,
    PIRP Irp
) {
    UNREFERENCED_PARAMETER(DeviceObjectParameter);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

NTSTATUS
Driver::DriverClose(
    PDEVICE_OBJECT DeviceObjectParameter,
    PIRP Irp
) {
    UNREFERENCED_PARAMETER(DeviceObjectParameter);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

NTSTATUS
Driver::DriverDeviceControl(
    PDEVICE_OBJECT DeviceObjectParameter,
    PIRP Irp
) {
    UNREFERENCED_PARAMETER(DeviceObjectParameter);

	PIO_STACK_LOCATION IoStackLocation;
    NTSTATUS status = STATUS_ACCESS_DENIED;

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    status = STATUS_INVALID_DEVICE_REQUEST;

    switch (IoStackLocation->Parameters.DeviceIoControl.IoControlCode)
    {
		//need to check Hypervisor is running or don't Load
        case IOCTL_HV_START:
            status = Vmm::LoadVMM();
            break;
        case IOCTL_HV_STOP:
            status = Vmm::UnLoadVMM();
            break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

NTSTATUS
Driver::DriverUnsupported(
    PDEVICE_OBJECT DeviceObjectParameter,
    PIRP Irp
) {
    UNREFERENCED_PARAMETER(DeviceObjectParameter);

    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}