#include "driver.h"
#include <initguid.h>
#include <devpkey.h>

static const GUID *const modes[] = {&AUDIO_SIGNALPROCESSINGMODE_RAW,
    &AUDIO_SIGNALPROCESSINGMODE_DEFAULT, &AUDIO_SIGNALPROCESSINGMODE_COMMUNICATIONS,
    &AUDIO_SIGNALPROCESSINGMODE_MEDIA, &AUDIO_SIGNALPROCESSINGMODE_MOVIE};

static VOID Append(WCHAR *text, ULONG *length, const WCHAR *value) {
  while (*value && *length < 95) text[(*length)++] = *value++;
  text[*length] = 0;
}
static VOID FriendlyName(const REY_DEVICE_CONTEXT *d, const REY_CIRCUIT_CONTEXT *c,
                          WCHAR *text) {
  ULONG length = 0, i;
  Append(text, &length, L"Rey Audio ");
  for (i = 24; i < 32; ++i) text[length++] = (WCHAR)d->profile.device_id[i];
  text[length] = 0;
  Append(text, &length, c->capture ? L" Input " : L" Output ");
  if (c->slot < 2) {
    text[length++] = L'1';
    if (c->channels > 1) {
      text[length++] = L'-'; text[length++] = (WCHAR)(L'0' + c->channels);
    }
    text[length] = 0; Append(text, &length, L" (Multichannel)");
  }
  else {
    text[length++] = (WCHAR)(L'1' + c->first_channel);
    if (c->channels == 2) {
      text[length++] = L'/';
      text[length++] = (WCHAR)(L'2' + c->first_channel);
    }
    text[length] = 0;
  }
}

NTSTATUS ReyCircuitInitialize(WDFDEVICE device, ACXCIRCUIT circuit, ACXOBJECTBAG properties) {
  const DEVPROPKEY packet_key = {
      {0x9404f781, 0x7191, 0x409b, {0x8b, 0x0b, 0x80, 0xbf, 0x6e, 0xc2, 0x29, 0xae}}, 2};
  struct {
    KSAUDIO_PACKETSIZE_CONSTRAINTS2 base;
    KSAUDIO_PACKETSIZE_PROCESSINGMODE_CONSTRAINT extra[4];
  } constraints;
  const REY_DEVICE_CONTEXT *d = ReyDeviceContext(device);
  const REY_CIRCUIT_CONTEXT *c = ReyCircuitContext(circuit);
  UNICODE_STRING acx_link = {0}, audio_link = {0};
  WDFSTRING name = AcxCircuitGetSymbolicLinkName(circuit);
  WCHAR friendly[96];
  ULONG i;
  NTSTATUS status;
  UNREFERENCED_PARAMETER(properties);
  if (!name) return STATUS_INVALID_DEVICE_STATE;
  WdfStringGetUnicodeString(name, &acx_link);
  if (!acx_link.Length) return STATUS_INVALID_DEVICE_STATE;
  RtlZeroMemory(&constraints, sizeof(constraints));
  constraints.base.MinPacketPeriodInHns =
      (ULONG)((ULONGLONG)d->profile.block * 10000000 / d->profile.rate);
  constraints.base.PacketSizeFileAlignment = FILE_LONG_ALIGNMENT;
  constraints.base.MaxPacketSizeInBytes = REY_BRIDGE_PACKET_FRAMES * c->channels * 4;
  constraints.base.NumProcessingModeConstraints = RTL_NUMBER_OF(modes);
  for (i = 0; i < RTL_NUMBER_OF(modes); ++i) {
    KSAUDIO_PACKETSIZE_PROCESSINGMODE_CONSTRAINT *p = i ? &constraints.extra[i - 1] :
        &constraints.base.ProcessingModeConstraints[0];
    p->ProcessingMode = *modes[i];
    p->SamplesPerProcessingPacket = d->profile.block;
  }
  status = IoGetDeviceInterfaceAlias(&acx_link, &KSCATEGORY_AUDIO, &audio_link);
  if (!NT_SUCCESS(status)) return status;
  status = IoSetDeviceInterfacePropertyData(&audio_link, &packet_key, 0, 0, DEVPROP_TYPE_BINARY,
                                            sizeof(constraints), &constraints);
  if (NT_SUCCESS(status)) {
    FriendlyName(d, c, friendly);
    status = IoSetDeviceInterfacePropertyData(&audio_link, &DEVPKEY_DeviceInterface_FriendlyName,
        0, 0, DEVPROP_TYPE_STRING, (ULONG)((wcslen(friendly) + 1) * sizeof(WCHAR)), friendly);
  }
  RtlFreeUnicodeString(&audio_link);
  return status;
}

NTSTATUS ReyCreateCircuit(WDFDEVICE device, ULONG slot, ACXCIRCUIT *result) {
  static const WCHAR *const names[REY_ENDPOINT_SLOTS] = {
      L"ReyAudioInput", L"ReyAudioOutput", L"ReyAudioInput1_2", L"ReyAudioOutput1_2",
      L"ReyAudioInput3_4", L"ReyAudioOutput3_4", L"ReyAudioInput5_6", L"ReyAudioOutput5_6",
      L"ReyAudioInput7_8", L"ReyAudioOutput7_8"};
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(device);
  ULONG channels = rey_endpoint_channels(slot, d->profile.inputs, d->profile.outputs), i;
  BOOLEAN capture = (BOOLEAN)rey_endpoint_capture(slot);
  GUID component = slot < 2 ? (capture ? GUID_REY_CAPTURE : GUID_REY_RENDER) :
      (capture ? GUID_REY_PAIR_CAPTURE : GUID_REY_PAIR_RENDER);
  UNICODE_STRING name;
  PACXCIRCUIT_INIT init = NULL;
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
  if (!channels) return STATUS_INVALID_PARAMETER;
  if (slot >= 2) component.Data1 += ((slot - 2) / 2) * 2;
  RtlInitUnicodeString(&name, names[slot]);
  init = AcxCircuitInitAllocate(device);
  if (!init) return STATUS_INSUFFICIENT_RESOURCES;
  AcxCircuitInitSetCircuitType(init, capture ? AcxCircuitTypeCapture : AcxCircuitTypeRender);
  AcxCircuitInitSetComponentId(init, &component);
  status = AcxCircuitInitAssignName(init, &name);
  if (!NT_SUCCESS(status)) goto failed;
  status = AcxCircuitInitAssignAcxCreateStreamCallback(init, ReyCreateStream);
  if (!NT_SUCCESS(status)) goto failed;
  ACX_CIRCUIT_COMPOSITE_CALLBACKS_INIT(&composite);
  composite.EvtAcxCircuitCompositeCircuitInitialize = ReyCircuitInitialize;
  AcxCircuitInitSetAcxCircuitCompositeCallbacks(init, &composite);
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, REY_CIRCUIT_CONTEXT);
  status = AcxCircuitCreate(device, &attributes, &init, &circuit);
  if (!NT_SUCCESS(status)) goto failed;
  ReyCircuitContext(circuit)->capture = capture;
  ReyCircuitContext(circuit)->slot = slot;
  ReyCircuitContext(circuit)->channels = channels;
  ReyCircuitContext(circuit)->first_channel = rey_endpoint_first_channel(slot);

  ACX_PIN_CONFIG_INIT(&pin_config);
  pin_config.Type = capture ? AcxPinTypeSource : AcxPinTypeSink;
  pin_config.Communication = AcxPinCommunicationSink;
  pin_config.Category = &KSCATEGORY_AUDIO;
  pin_config.MaxStreams = 1;
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = circuit;
  status = AcxPinCreate(circuit, &attributes, &pin_config, &pins[0]);
  if (!NT_SUCCESS(status)) goto failed;
  ACX_PIN_CONFIG_INIT(&pin_config);
  pin_config.Type = capture ? AcxPinTypeSink : AcxPinTypeSource;
  pin_config.Communication = AcxPinCommunicationNone;
  pin_config.Category = capture ? (slot < 2 ? &KSNODETYPE_LINE_CONNECTOR : &KSNODETYPE_MICROPHONE) :
      &KSNODETYPE_SPEAKER;
  status = AcxPinCreate(circuit, &attributes, &pin_config, &pins[1]);
  if (!NT_SUCCESS(status)) goto failed;

  RtlZeroMemory(&wave, sizeof(wave));
  wave.DataFormat.FormatSize = sizeof(wave);
  wave.DataFormat.SampleSize = channels * 4;
  wave.DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
  wave.DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  wave.DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
  wave.WaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wave.WaveFormatExt.Format.nChannels = (WORD)channels;
  wave.WaveFormatExt.Format.nSamplesPerSec = d->profile.rate;
  wave.WaveFormatExt.Format.nBlockAlign = (WORD)(channels * 4);
  wave.WaveFormatExt.Format.nAvgBytesPerSec = d->profile.rate * channels * 4;
  wave.WaveFormatExt.Format.wBitsPerSample = 32;
  wave.WaveFormatExt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  wave.WaveFormatExt.Samples.wValidBitsPerSample = (WORD)d->profile.valid_bits;
  wave.WaveFormatExt.dwChannelMask = slot < 2 ? KSAUDIO_SPEAKER_DIRECTOUT :
      (channels == 2 ? KSAUDIO_SPEAKER_STEREO : KSAUDIO_SPEAKER_MONO);
  wave.WaveFormatExt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
  ACX_DATAFORMAT_CONFIG_INIT_KS(&format_config, &wave);
  status = AcxDataFormatCreate(device, &attributes, &format_config, &format);
  if (!NT_SUCCESS(status)) goto failed;
  formats = AcxPinGetRawDataFormatList(pins[0]);
  if (!formats) { status = STATUS_INSUFFICIENT_RESOURCES; goto failed; }
  status = AcxDataFormatListAddDataFormat(formats, format);
  if (!NT_SUCCESS(status)) goto failed;
  status = AcxDataFormatListAssignDefaultDataFormat(formats, format);
  if (!NT_SUCCESS(status)) goto failed;
  for (i = 1; i < RTL_NUMBER_OF(modes); ++i) {
    status = AcxPinAssignModeDataFormatList(pins[0], modes[i], formats);
    if (!NT_SUCCESS(status)) goto failed;
  }
  status = AcxCircuitAddPins(circuit, pins, 2);
  if (!NT_SUCCESS(status)) goto failed;
  *result = circuit;
  return STATUS_SUCCESS;
failed:
  if (init) AcxCircuitInitFree(init);
  if (circuit) WdfObjectDelete(circuit);
  return status;
}
