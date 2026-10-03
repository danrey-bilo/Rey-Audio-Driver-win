#include "driver.h"
NTSTATUS DriverEntry(PDRIVER_OBJECT object, PUNICODE_STRING path) {
  WDF_DRIVER_CONFIG config;
  ACX_DRIVER_CONFIG acx;
  WDFDRIVER driver;
  NTSTATUS status;
  WDF_DRIVER_CONFIG_INIT(&config, ReyDeviceAdd);
  status = WdfDriverCreate(object, path, WDF_NO_OBJECT_ATTRIBUTES, &config, &driver);
  if (!NT_SUCCESS(status))
    return status;
  ACX_DRIVER_CONFIG_INIT(&acx);
  return AcxDriverInitialize(driver, &acx);
}
