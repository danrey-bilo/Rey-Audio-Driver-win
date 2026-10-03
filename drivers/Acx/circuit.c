#include "driver.h"

NTSTATUS PiaoipCircuitInitialize(WDFDEVICE device, ACXCIRCUIT circuit, ACXOBJECTBAG properties) {
  const DEVPROPKEY packet_key = {
      {0x9404f781, 0x7191, 0x409b, {0x8b, 0x0b, 0x80, 0xbf, 0x6e, 0xc2, 0x29, 0xae}}, 2};
  struct {
    KSAUDIO_PACKETSIZE_CONSTRAINTS2 base;
    KSAUDIO_PACKETSIZE_PROCESSINGMODE_CONSTRAINT extra;
  } constraints;
  const PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(device);
  UNICODE_STRING acx_link = {0}, audio_link = {0};
  WDFSTRING name = AcxCircuitGetSymbolicLinkName(circuit);
  ULONG channels = PiaoipCircuitContext(circuit)->capture ? d->profile.inputs : d->profile.outputs;
  NTSTATUS status;
  UNREFERENCED_PARAMETER(properties);
  if (!name)
    return STATUS_INVALID_DEVICE_STATE;
  WdfStringGetUnicodeString(name, &acx_link);
  if (!acx_link.Length)
    return STATUS_INVALID_DEVICE_STATE;
  RtlZeroMemory(&constraints, sizeof(constraints));
  constraints.base.MinPacketPeriodInHns =
      (ULONG)((ULONGLONG)d->profile.block * 10000000 / d->profile.rate);
  constraints.base.PacketSizeFileAlignment = FILE_LONG_ALIGNMENT;
  constraints.base.MaxPacketSizeInBytes = PIAOIP_BRIDGE_PACKET_FRAMES * channels * 4;
  constraints.base.NumProcessingModeConstraints = 2;
  constraints.base.ProcessingModeConstraints[0].ProcessingMode = AUDIO_SIGNALPROCESSINGMODE_RAW;
  constraints.base.ProcessingModeConstraints[0].SamplesPerProcessingPacket = d->profile.block;
  constraints.extra.ProcessingMode = AUDIO_SIGNALPROCESSINGMODE_DEFAULT;
  constraints.extra.SamplesPerProcessingPacket = d->profile.block;
  // Publish the manually selected period on the interface ACX has just created.
  // The WASAPI probe must verify what Windows actually offers to the client.
  status = IoGetDeviceInterfaceAlias(&acx_link, &KSCATEGORY_AUDIO, &audio_link);
  if (!NT_SUCCESS(status))
    return status;
  status = IoSetDeviceInterfacePropertyData(&audio_link, &packet_key, 0, 0, DEVPROP_TYPE_BINARY,
                                            sizeof(constraints), &constraints);
  RtlFreeUnicodeString(&audio_link);
  return status;
}

NTSTATUS PiaoipCreateCircuit(WDFDEVICE device, BOOLEAN capture, ACXCIRCUIT *result) {
  const UNICODE_STRING capture_name = RTL_CONSTANT_STRING(L"PiAoipCapture");
  const UNICODE_STRING render_name = RTL_CONSTANT_STRING(L"PiAoipRender");
  PIAOIP_DEVICE_CONTEXT *context = PiaoipDeviceContext(device);
  PACXCIRCUIT_INIT init;
  WDF_OBJECT_ATTRIBUTES attributes;
  ACXCIRCUIT circuit = NULL;
  ACX_PIN_CONFIG pin_config;
  ACXPIN pins[2];
  ACX_DATAFORMAT_CONFIG format_config;
  ACXDATAFORMAT format;
  ACXDATAFORMATLIST formats;
  ACX_CIRCUIT_COMPOSITE_CALLBACKS composite;
  KSDATAFORMAT_WAVEFORMATEXTENSIBLE wave;
  NTSTATUS status;
  ULONG channels = capture ? context->profile.inputs : context->profile.outputs;

  init = AcxCircuitInitAllocate(device);
  if (!init)
    return STATUS_INSUFFICIENT_RESOURCES;
  AcxCircuitInitSetCircuitType(init, capture ? AcxCircuitTypeCapture : AcxCircuitTypeRender);
  AcxCircuitInitSetComponentId(init, capture ? &GUID_PIAOIP_CAPTURE : &GUID_PIAOIP_RENDER);
  status = AcxCircuitInitAssignName(init, capture ? &capture_name : &render_name);
  if (!NT_SUCCESS(status))
    goto failed;
  status = AcxCircuitInitAssignAcxCreateStreamCallback(init, PiaoipCreateStream);
  if (!NT_SUCCESS(status))
    goto failed;
  ACX_CIRCUIT_COMPOSITE_CALLBACKS_INIT(&composite);
  composite.EvtAcxCircuitCompositeCircuitInitialize = PiaoipCircuitInitialize;
  AcxCircuitInitSetAcxCircuitCompositeCallbacks(init, &composite);
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, PIAOIP_CIRCUIT_CONTEXT);
  status = AcxCircuitCreate(device, &attributes, &init, &circuit);
  if (!NT_SUCCESS(status))
    goto failed;
  PiaoipCircuitContext(circuit)->capture = capture;

  ACX_PIN_CONFIG_INIT(&pin_config);
  pin_config.Type = capture ? AcxPinTypeSource : AcxPinTypeSink;
  pin_config.Communication = AcxPinCommunicationSink;
  pin_config.Category = &KSCATEGORY_AUDIO;
  pin_config.MaxStreams = 1;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = circuit;
  status = AcxPinCreate(circuit, &attributes, &pin_config, &pins[0]);
  if (!NT_SUCCESS(status))
    goto failed;
  ACX_PIN_CONFIG_INIT(&pin_config);
  pin_config.Type = capture ? AcxPinTypeSink : AcxPinTypeSource;
  pin_config.Communication = AcxPinCommunicationNone;
  pin_config.Category = capture ? &KSNODETYPE_LINE_CONNECTOR : &KSNODETYPE_SPEAKER;
  status = AcxPinCreate(circuit, &attributes, &pin_config, &pins[1]);
  if (!NT_SUCCESS(status))
    goto failed;

  RtlZeroMemory(&wave, sizeof(wave));
  wave.DataFormat.FormatSize = sizeof(wave);
  wave.DataFormat.SampleSize = channels * 4;
  wave.DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
  wave.DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  wave.DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
  wave.WaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wave.WaveFormatExt.Format.nChannels = (WORD)channels;
  wave.WaveFormatExt.Format.nSamplesPerSec = context->profile.rate;
  wave.WaveFormatExt.Format.nBlockAlign = (WORD)(channels * 4);
  wave.WaveFormatExt.Format.nAvgBytesPerSec = context->profile.rate * channels * 4;
  wave.WaveFormatExt.Format.wBitsPerSample = 32;
  wave.WaveFormatExt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  wave.WaveFormatExt.Samples.wValidBitsPerSample = (WORD)context->profile.valid_bits;
  wave.WaveFormatExt.dwChannelMask = KSAUDIO_SPEAKER_DIRECTOUT;
  wave.WaveFormatExt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  ACX_DATAFORMAT_CONFIG_INIT_KS(&format_config, &wave);
  status = AcxDataFormatCreate(device, &attributes, &format_config, &format);
  if (!NT_SUCCESS(status))
    goto failed;
  formats = AcxPinGetRawDataFormatList(pins[0]);
  if (!formats) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto failed;
  }
  status = AcxDataFormatListAddDataFormat(formats, format);
  if (!NT_SUCCESS(status))
    goto failed;
  status = AcxDataFormatListAssignDefaultDataFormat(formats, format);
  if (!NT_SUCCESS(status))
    goto failed;
  status = AcxPinAssignModeDataFormatList(pins[0], &AUDIO_SIGNALPROCESSINGMODE_DEFAULT, formats);
  if (!NT_SUCCESS(status))
    goto failed;
  status = AcxCircuitAddPins(circuit, pins, 2);
  if (!NT_SUCCESS(status))
    goto failed;
  *result = circuit;
  return STATUS_SUCCESS;
failed:
  if (init)
    AcxCircuitInitFree(init);
  if (circuit)
    WdfObjectDelete(circuit);
  return status;
}
