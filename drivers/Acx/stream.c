#include "driver.h"

static PBYTE PacketBuffer(PIAOIP_STREAM_CONTEXT *s, ULONG number) {
  PACX_RTPACKET p = &s->packets[number % s->packet_count];
  return (PBYTE)s->buffers[number % s->packet_count] + p->RtPacketOffset;
}
// Caller holds the child PCM lock. A callback from an old stream must not
// overwrite the running flag of a newer stream already published in its slot.
static VOID SetRunning(ACXSTREAM stream, BOOLEAN running) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  s->running = running;
  if (s->capture && d->capture_stream == stream)
    d->stats.capture_running = running;
  else if (!s->capture && d->render_stream == stream)
    d->stats.render_running = running;
}
NTSTATUS PiaoipCreateStream(WDFDEVICE device, ACXCIRCUIT circuit, ACXPIN pin, PACXSTREAM_INIT init,
                            ACXDATAFORMAT format, const GUID *mode, ACXOBJECTBAG arguments) {
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(device);
  BOOLEAN capture = PiaoipCircuitContext(circuit)->capture;
  WAVEFORMATEXTENSIBLE *wave = (WAVEFORMATEXTENSIBLE *)AcxDataFormatGetWaveFormatExtensible(format);
  WDF_OBJECT_ATTRIBUTES attributes;
  ACX_STREAM_CALLBACKS callbacks;
  ACX_RT_STREAM_CALLBACKS realtime;
  ACXSTREAM stream = NULL;
  NTSTATUS status;
  ULONG channels = capture ? d->profile.inputs : d->profile.outputs;
  BOOLEAN *creating = capture ? &d->capture_creating : &d->render_creating;
  UNREFERENCED_PARAMETER(pin);
  UNREFERENCED_PARAMETER(arguments);
  if (!mode ||
      (!IsEqualGUID(mode, &AUDIO_SIGNALPROCESSINGMODE_RAW) &&
       !IsEqualGUID(mode, &AUDIO_SIGNALPROCESSINGMODE_DEFAULT)) ||
      !wave || wave->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE || wave->Format.cbSize < 22 ||
      wave->Format.nChannels != channels || wave->Format.nSamplesPerSec != d->profile.rate ||
      wave->Format.wBitsPerSample != 32 || wave->Format.nBlockAlign != channels * 4 ||
      wave->Samples.wValidBitsPerSample != d->profile.valid_bits ||
      !IsEqualGUID(&wave->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
    return STATUS_NO_MATCH;
  WdfWaitLockAcquire(d->lock, NULL);
  if (d->removing || *creating || (capture ? d->capture_stream : d->render_stream)) {
    WdfWaitLockRelease(d->lock);
    return STATUS_DEVICE_BUSY;
  }
  *creating = TRUE;
  WdfWaitLockRelease(d->lock);
  // Reserve the stream slot, then perform ACX allocation without holding the
  // PCM exchange lock used by the other direction's already running stream.
  ACX_STREAM_CALLBACKS_INIT(&callbacks);
  callbacks.EvtAcxStreamPrepareHardware = PiaoipStreamPrepare;
  callbacks.EvtAcxStreamReleaseHardware = PiaoipStreamRelease;
  callbacks.EvtAcxStreamRun = PiaoipStreamRun;
  callbacks.EvtAcxStreamPause = PiaoipStreamPause;
  status = AcxStreamInitAssignAcxStreamCallbacks(init, &callbacks);
  if (!NT_SUCCESS(status))
    goto done;
  ACX_RT_STREAM_CALLBACKS_INIT(&realtime);
  realtime.EvtAcxStreamAllocateRtPackets = PiaoipAllocatePackets;
  realtime.EvtAcxStreamFreeRtPackets = PiaoipFreePackets;
  realtime.EvtAcxStreamGetHwLatency = PiaoipStreamLatency;
  realtime.EvtAcxStreamGetCurrentPacket = PiaoipGetCurrentPacket;
  realtime.EvtAcxStreamGetPresentationPosition = PiaoipGetPosition;
  if (capture)
    realtime.EvtAcxStreamGetCapturePacket = PiaoipGetCapturePacket;
  else
    realtime.EvtAcxStreamSetRenderPacket = PiaoipSetRenderPacket;
  status = AcxStreamInitAssignAcxRtStreamCallbacks(init, &realtime);
  if (!NT_SUCCESS(status))
    goto done;
  AcxStreamInitSetAcxRtStreamSupportsNotifications(init);
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, PIAOIP_STREAM_CONTEXT);
  attributes.EvtDestroyCallback = PiaoipStreamDestroy;
  status = AcxRtStreamCreate(device, circuit, &attributes, &init, &stream);
  if (NT_SUCCESS(status)) {
    PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
    s->device = device;
    s->capture = capture;
    s->channels = channels;
  }
done:
  WdfWaitLockAcquire(d->lock, NULL);
  *creating = FALSE;
  if (NT_SUCCESS(status)) {
    if (d->removing)
      status = STATUS_DEVICE_NOT_CONNECTED;
    else if (capture)
      d->capture_stream = stream;
    else
      d->render_stream = stream;
  }
  WdfWaitLockRelease(d->lock);
  if (!NT_SUCCESS(status) && stream)
    WdfObjectDelete(stream);
  return status;
}
VOID PiaoipStreamDestroy(WDFOBJECT object) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext((ACXSTREAM)object);
  if (s->device) {
    PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
    WdfWaitLockAcquire(d->lock, NULL);
    SetRunning((ACXSTREAM)object, FALSE);
    if (d->capture_stream == (ACXSTREAM)object)
      d->capture_stream = NULL;
    if (d->render_stream == (ACXSTREAM)object)
      d->render_stream = NULL;
    WdfWaitLockRelease(d->lock);
    if (s->packets)
      PiaoipFreePackets((ACXSTREAM)object, s->packets, s->packet_count);
  }
}
static VOID FreePacketStorage(PACX_RTPACKET packets, PVOID *buffers, ULONG count) {
  ULONG i;
  if (!packets)
    return;
  for (i = 0; i < count; ++i)
    if (packets[i].RtPacketBuffer.u.MdlType.Mdl) {
      PMDL mdl = packets[i].RtPacketBuffer.u.MdlType.Mdl;
      MmUnmapLockedPages(buffers[i], mdl);
      MmFreePagesFromMdl(mdl);
      ExFreePool(mdl);
    }
  ExFreePoolWithTag(packets, PIAOIP_POOL_TAG);
}
NTSTATUS PiaoipAllocatePackets(ACXSTREAM stream, ULONG count, ULONG bytes, PACX_RTPACKET *result) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  PACX_RTPACKET packets = NULL;
  PVOID buffers[2] = {NULL, NULL};
  ULONG pages, i;
  PHYSICAL_ADDRESS low = {0}, high, skip = {0};
  NTSTATUS status = STATUS_SUCCESS;
  if (!count || count > 2 || !bytes || (count == 1 && bytes % PAGE_SIZE) ||
      bytes % (s->channels * 4) || bytes / (s->channels * 4) < d->profile.block ||
      bytes / (s->channels * 4) > PIAOIP_BRIDGE_PACKET_FRAMES ||
      (bytes / (s->channels * 4)) % d->profile.block)
    return STATUS_INVALID_PARAMETER;
  WdfWaitLockAcquire(d->lock, NULL);
  if (s->packets || s->allocating || s->running || d->removing) {
    WdfWaitLockRelease(d->lock);
    return STATUS_INVALID_DEVICE_STATE;
  }
  s->allocating = TRUE;
  WdfObjectReference(stream);
  WdfWaitLockRelease(d->lock);
  // These private pages are prepared before publication. PCM exchange sees
  // either no packets or a complete allocation and never waits for the MM.
  packets = (PACX_RTPACKET)ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(*packets),
                                           PIAOIP_POOL_TAG);
  if (!packets) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto publish;
  }
  pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
  high.QuadPart = MAXLONGLONG;
  for (i = 0; i < count; ++i) {
    PMDL mdl;
    PVOID buffer;
    ACX_RTPACKET_INIT(&packets[i]);
    // Allocate private, zeroed pages for the WaveRT mapping. Avoid mapping any
    // neighbouring pool allocation into an ordinary audio client's address space.
    mdl = MmAllocatePagesForMdlEx(low, high, skip, pages, MmCached, MM_ALLOCATE_FULLY_REQUIRED);
    if (!mdl) {
      status = STATUS_INSUFFICIENT_RESOURCES;
      break;
    }
    buffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
    if (!buffer) {
      MmFreePagesFromMdl(mdl);
      ExFreePool(mdl);
      status = STATUS_INSUFFICIENT_RESOURCES;
      break;
    }
    buffers[i] = buffer;
    WDF_MEMORY_DESCRIPTOR_INIT_MDL(&packets[i].RtPacketBuffer, mdl, pages);
    packets[i].RtPacketSize = bytes;
    packets[i].RtPacketOffset = i == 0 ? pages - bytes : 0;
  }
publish:
  WdfWaitLockAcquire(d->lock, NULL);
  s->allocating = FALSE;
  if (NT_SUCCESS(status) && d->removing)
    status = STATUS_DEVICE_NOT_CONNECTED;
  if (NT_SUCCESS(status)) {
    s->packets = packets;
    s->packet_count = count;
    s->packet_bytes = bytes;
    s->packet_frames = bytes / (s->channels * 4);
    RtlCopyMemory(s->buffers, buffers, sizeof(buffers));
    *result = packets;
  }
  WdfWaitLockRelease(d->lock);
  if (!NT_SUCCESS(status))
    FreePacketStorage(packets, buffers, count);
  WdfObjectDereference(stream);
  return status;
}
VOID PiaoipFreePackets(ACXSTREAM stream, PACX_RTPACKET packets, ULONG count) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  PVOID buffers[2] = {NULL, NULL};
  BOOLEAN owned = FALSE;
  WdfWaitLockAcquire(d->lock, NULL);
  if (s->packets == packets) {
    count = s->packet_count;
    SetRunning(stream, FALSE);
    s->prepared = FALSE;
    s->packets = NULL;
    s->packet_count = 0;
    RtlCopyMemory(buffers, s->buffers, sizeof(buffers));
    RtlZeroMemory(s->buffers, sizeof(s->buffers));
    owned = TRUE;
  }
  WdfWaitLockRelease(d->lock);
  if (owned)
    FreePacketStorage(packets, buffers, count);
}
NTSTATUS PiaoipStreamPrepare(ACXSTREAM stream) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  s->current_packet = 0;
  s->reported_packet = 0;
  s->partial_frames = 0;
  s->position = 0;
  s->qpc = 0;
  RtlZeroMemory(s->render_valid, sizeof(s->render_valid));
  s->prepared = TRUE;
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
NTSTATUS PiaoipStreamRelease(ACXSTREAM stream) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  s->prepared = FALSE;
  SetRunning(stream, FALSE);
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
NTSTATUS PiaoipStreamRun(ACXSTREAM stream) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  NTSTATUS status = STATUS_SUCCESS;
  WdfWaitLockAcquire(d->lock, NULL);
  if (!s->prepared || !s->packets || d->removing ||
      (s->capture ? d->capture_stream != stream : d->render_stream != stream))
    status = STATUS_DEVICE_NOT_READY;
  else {
    SetRunning(stream, TRUE);
  }
  WdfWaitLockRelease(d->lock);
  return status;
}
NTSTATUS PiaoipStreamPause(ACXSTREAM stream) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  SetRunning(stream, FALSE);
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
NTSTATUS PiaoipStreamLatency(ACXSTREAM stream, ULONG *fifo, ULONG *delay) {
  UNREFERENCED_PARAMETER(stream);
  *fifo = 0;
  *delay = 0;
  return STATUS_SUCCESS;
}
NTSTATUS PiaoipSetRenderPacket(ACXSTREAM stream, ULONG packet, ULONG flags, ULONG eos_length) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  NTSTATUS status = STATUS_SUCCESS;
  ULONG index;
  WdfWaitLockAcquire(d->lock, NULL);
  if (!s->packets || flags & ~KSSTREAM_HEADER_OPTIONSF_ENDOFSTREAM ||
      (flags && (eos_length > s->packet_bytes || eos_length % (s->channels * 4))))
    status = STATUS_INVALID_PARAMETER;
  else if ((LONG)(packet - s->current_packet) < 0 ||
           (packet == s->current_packet && s->partial_frames))
    status = STATUS_DATA_LATE_ERROR;
  else if (packet - s->current_packet >= s->packet_count)
    status = STATUS_DATA_OVERRUN;
  else {
    index = packet % s->packet_count;
    s->render_ready[index] = packet;
    s->render_valid[index] = TRUE;
    s->render_length[index] = flags ? eos_length / (s->channels * 4) : s->packet_frames;
  }
  WdfWaitLockRelease(d->lock);
  return status;
}
NTSTATUS PiaoipGetCurrentPacket(ACXSTREAM stream, ULONG *packet) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  // current_packet owns the next/incomplete buffer. Before its first frame,
  // the last completed packet is ULONG_MAX, so the OS may prefill packet zero.
  *packet = s->partial_frames ? s->current_packet : s->current_packet - 1;
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
NTSTATUS PiaoipGetCapturePacket(ACXSTREAM stream, ULONG *packet, ULONGLONG *qpc, BOOLEAN *more) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  NTSTATUS status = STATUS_SUCCESS;
  WdfWaitLockAcquire(d->lock, NULL);
  if (!s->packet_frames || s->position < s->packet_frames)
    status = STATUS_DEVICE_NOT_READY;
  else {
    *packet = s->current_packet - 1;
    *qpc = s->last_capture_qpc;
    *more = FALSE;
    s->reported_packet = s->current_packet;
  }
  WdfWaitLockRelease(d->lock);
  return status;
}
NTSTATUS PiaoipGetPosition(ACXSTREAM stream, ULONGLONG *position, ULONGLONG *qpc) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  *position = s->position;
  *qpc = s->qpc;
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}

static VOID AdvanceStream(ACXSTREAM stream, const int32_t *input, int32_t *output, ULONG frames,
                          ULONGLONG qpc, PIAOIP_BRIDGE_STATS *stats) {
  PIAOIP_STREAM_CONTEXT *s = PiaoipStreamContext(stream);
  ULONG offset = 0;
  while (offset < frames) {
    ULONG available = s->packet_frames - s->partial_frames, chunk = min(available, frames - offset);
    ULONG index = s->current_packet % s->packet_count;
    PBYTE packet = PacketBuffer(s, s->current_packet) + s->partial_frames * s->channels * 4;
    if (s->capture) {
      if (!s->partial_frames && s->current_packet - s->reported_packet >= s->packet_count)
        ++stats->capture_overruns;
      if (input)
        RtlCopyMemory(packet, input + (SIZE_T)offset * s->channels,
                      (SIZE_T)chunk * s->channels * 4);
      else
        RtlZeroMemory(packet, (SIZE_T)chunk * s->channels * 4);
      if (!s->partial_frames)
        s->last_capture_qpc = qpc;
      stats->capture_frames += chunk;
    } else {
      BOOLEAN ready = s->render_valid[index] && s->render_ready[index] == s->current_packet;
      ULONG length = ready && s->render_length[index] > s->partial_frames
                         ? min(chunk, s->render_length[index] - s->partial_frames)
                         : 0;
      if (length && output)
        RtlCopyMemory(output + (SIZE_T)offset * s->channels, packet,
                      (SIZE_T)length * s->channels * 4);
      if (!ready && output)
        stats->render_underruns += chunk;
      stats->render_frames += chunk;
    }
    s->partial_frames += chunk;
    s->position += chunk;
    s->qpc = qpc;
    offset += chunk;
    if (s->partial_frames == s->packet_frames) {
      if (!s->capture)
        s->render_valid[index] = FALSE;
      (VOID) AcxRtStreamNotifyPacketComplete(stream, s->current_packet, qpc);
      ++s->current_packet;
      s->partial_frames = 0;
    }
  }
}
NTSTATUS PiaoipExchange(WDFDEVICE child, const PIAOIP_BRIDGE_EXCHANGE *input,
                        PIAOIP_BRIDGE_EXCHANGE *output) {
  PIAOIP_DEVICE_CONTEXT *d = PiaoipDeviceContext(child);
  ULONG frames = input->frames, flags = 0;
  uint32_t gap = 0;
  ULONGLONG qpc = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
  if (!piaoip_bridge_valid_exchange(input, &d->profile))
    return STATUS_INVALID_PARAMETER;
  WdfWaitLockAcquire(d->lock, NULL);
  if (d->removing) {
    WdfWaitLockRelease(d->lock);
    return STATUS_DEVICE_NOT_CONNECTED;
  }
  if (d->bridge_seen &&
      (input->epoch != d->bridge_epoch ||
       !piaoip_bridge_frame_gap(input, d->next_bridge_frame, d->profile.block * 4, &gap))) {
    WdfWaitLockRelease(d->lock);
    return STATUS_DATA_ERROR;
  }
  d->bridge_seen = TRUE;
  d->bridge_epoch = (USHORT)input->epoch;
  d->next_bridge_frame = input->frame_position + frames;
  if (input->flags)
    ++d->stats.discontinuities;
  if (d->capture_stream && PiaoipStreamContext(d->capture_stream)->running) {
    if (gap)
      AdvanceStream(d->capture_stream, NULL, NULL, gap, qpc, &d->stats);
    AdvanceStream(d->capture_stream, input->samples, NULL, frames, qpc, &d->stats);
    flags |= PIAOIP_BRIDGE_CAPTURE_ACTIVE;
  }
  // Both METHOD_BUFFERED directions may alias. Capture has consumed the input.
  RtlZeroMemory(output->samples, sizeof(output->samples));
  if (d->render_stream && PiaoipStreamContext(d->render_stream)->running) {
    if (gap)
      AdvanceStream(d->render_stream, NULL, NULL, gap, qpc, &d->stats);
    AdvanceStream(d->render_stream, NULL, output->samples, frames, qpc, &d->stats);
    flags |= PIAOIP_BRIDGE_RENDER_ACTIVE;
  }
  output->flags = flags;
  output->qpc = qpc;
  ++d->stats.exchanges;
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
