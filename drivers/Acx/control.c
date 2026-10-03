#include "driver.h"

VOID PiaoipIoControl(WDFQUEUE queue, WDFREQUEST request, size_t output_length, size_t input_length,
                     ULONG code) {
  WDFDEVICE root = WdfIoQueueGetDevice(queue), child = NULL;
  PIAOIP_DEVICE_CONTEXT *context = PiaoipDeviceContext(root);
  NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
  ULONG_PTR bytes = 0;
  PVOID input = NULL, output = NULL;
  UNREFERENCED_PARAMETER(input_length);
  UNREFERENCED_PARAMETER(output_length);
  if (code == IOCTL_PIAOIP_ATTACH) {
    status = WdfRequestRetrieveInputBuffer(request, sizeof(PIAOIP_BRIDGE_PROFILE), &input, NULL);
    if (NT_SUCCESS(status))
      status = piaoip_bridge_valid_profile((const PIAOIP_BRIDGE_PROFILE *)input)
                   ? PiaoipCreateChild(root, (const PIAOIP_BRIDGE_PROFILE *)input)
                   : STATUS_INVALID_PARAMETER;
  } else if (code == IOCTL_PIAOIP_DETACH)
    status = PiaoipDetachChild(root);
  else if (code == IOCTL_PIAOIP_EXCHANGE || code == IOCTL_PIAOIP_STATS) {
    WdfWaitLockAcquire(context->lock, NULL);
    child = context->child;
    if (child)
      WdfObjectReference(child);
    WdfWaitLockRelease(context->lock);
    if (!child)
      status = STATUS_DEVICE_NOT_CONNECTED;
    else if (code == IOCTL_PIAOIP_EXCHANGE) {
      status = WdfRequestRetrieveInputBuffer(request, sizeof(PIAOIP_BRIDGE_EXCHANGE), &input, NULL);
      if (NT_SUCCESS(status))
        status =
            WdfRequestRetrieveOutputBuffer(request, sizeof(PIAOIP_BRIDGE_EXCHANGE), &output, NULL);
      // METHOD_BUFFERED aliases both buffers. PiaoipExchange first consumes
      // capture PCM, then writes the bounded render reply into the same storage.
      if (NT_SUCCESS(status))
        status = PiaoipExchange(child, (const PIAOIP_BRIDGE_EXCHANGE *)input,
                                (PIAOIP_BRIDGE_EXCHANGE *)output);
      if (NT_SUCCESS(status))
        bytes = sizeof(PIAOIP_BRIDGE_EXCHANGE);
    } else {
      status = WdfRequestRetrieveOutputBuffer(request, sizeof(PIAOIP_BRIDGE_STATS), &output, NULL);
      if (NT_SUCCESS(status)) {
        PIAOIP_DEVICE_CONTEXT *c = PiaoipDeviceContext(child);
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
