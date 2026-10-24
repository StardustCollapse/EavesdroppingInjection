#include "Driver/Driver.h"
#include "Driver/KernelBenchmark.h"
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
    ULONG_PTR information = 0;

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    status = STATUS_INVALID_DEVICE_REQUEST;

    switch (IoStackLocation->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_HV_START:
            status = Vmm::LoadVMM();
            break;
        case IOCTL_HV_STOP:
            status = Vmm::UnLoadVMM();
            break;
        case IOCTL_HV_BENCHMARK:
        {
            if (IoStackLocation->Parameters.DeviceIoControl.InputBufferLength >= sizeof(KERNEL_BENCHMARK_REQUEST) &&
                IoStackLocation->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(KERNEL_BENCHMARK_RESPONSE) &&
                Irp->AssociatedIrp.SystemBuffer != NULL)
            {
                PKERNEL_BENCHMARK_REQUEST req = (PKERNEL_BENCHMARK_REQUEST)Irp->AssociatedIrp.SystemBuffer;
                PKERNEL_BENCHMARK_RESPONSE resp = (PKERNEL_BENCHMARK_RESPONSE)Irp->AssociatedIrp.SystemBuffer;

                ULONG iters = req->Iterations;
                if (iters == 0) iters = 10000;
                if (iters > 100000) iters = 100000;

                KernelBenchmark::Execute(iters, resp);

                status = STATUS_SUCCESS;
                information = sizeof(KERNEL_BENCHMARK_RESPONSE);
            }
            else
            {
                status = STATUS_BUFFER_TOO_SMALL;
                information = 0;
            }
            break;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
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
