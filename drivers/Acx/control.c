#include "driver.h"

VOID ReyIoControl(WDFQUEUE queue, WDFREQUEST request, size_t output_length, size_t input_length,
                     ULONG code) {
  WDFDEVICE root = WdfIoQueueGetDevice(queue), child = NULL;
  WDFFILEOBJECT file = WdfRequestGetFileObject(request);
  NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
  ULONG_PTR bytes = 0;
  PVOID input = NULL, output = NULL;
  UNREFERENCED_PARAMETER(input_length);
  UNREFERENCED_PARAMETER(output_length);
  if (!file) {
    WdfRequestComplete(request, STATUS_INVALID_HANDLE);
    return;
  }
  if (code == IOCTL_REY_ATTACH) {
    status = WdfRequestRetrieveInputBuffer(request, sizeof(REY_BRIDGE_PROFILE), &input, NULL);
    if (NT_SUCCESS(status))
      status = rey_bridge_valid_profile((const REY_BRIDGE_PROFILE *)input)
                   ? ReyCreateChild(root, file, (const REY_BRIDGE_PROFILE *)input)
                   : STATUS_INVALID_PARAMETER;
  } else if (code == IOCTL_REY_DETACH)
    status = ReyDetachChild(file);
  else if (code == IOCTL_REY_EXCHANGE || code == IOCTL_REY_STATS) {
    child = ReyReferenceChild(file);
    if (!child)
      status = STATUS_DEVICE_NOT_CONNECTED;
    else if (code == IOCTL_REY_EXCHANGE) {
      status = WdfRequestRetrieveInputBuffer(request, sizeof(REY_BRIDGE_EXCHANGE), &input, NULL);
      if (NT_SUCCESS(status))
        status =
            WdfRequestRetrieveOutputBuffer(request, sizeof(REY_BRIDGE_EXCHANGE), &output, NULL);
      // METHOD_BUFFERED aliases both buffers. ReyExchange first consumes
      // capture PCM, then writes the bounded render reply into the same storage.
      if (NT_SUCCESS(status))
        status = ReyExchange(child, (const REY_BRIDGE_EXCHANGE *)input,
                                (REY_BRIDGE_EXCHANGE *)output);
      if (NT_SUCCESS(status))
        bytes = sizeof(REY_BRIDGE_EXCHANGE);
    } else {
      status = WdfRequestRetrieveOutputBuffer(request, sizeof(REY_BRIDGE_STATS), &output, NULL);
      if (NT_SUCCESS(status)) {
        REY_DEVICE_CONTEXT *c = ReyDeviceContext(child);
        WdfWaitLockAcquire(c->lock, NULL);
        RtlCopyMemory(output, &c->stats, sizeof(c->stats));
        WdfWaitLockRelease(c->lock);
        bytes = sizeof(c->stats);
      }
    }
    if (child)
      WdfObjectDereference(child);
  }
  WdfRequestCompleteWithInformation(request, status, bytes);
}
