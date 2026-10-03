#include "driver.h"

static PBYTE PacketBuffer(REY_STREAM_CONTEXT *s, ULONG number) {
  PACX_RTPACKET p = &s->packets[number % s->packet_count];
  return (PBYTE)s->buffers[number % s->packet_count] + p->RtPacketOffset;
}
// Caller holds the child PCM lock. A callback from an old stream must not
// overwrite the running flag of a newer stream already published in its slot.
static VOID SetRunning(ACXSTREAM stream, BOOLEAN running) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
  ULONG i;
  s->running = running;
  d->stats.capture_running = d->stats.render_running = 0;
  for (i = 0; i < REY_ENDPOINT_SLOTS; ++i)
    if (d->streams[i] && ReyStreamContext(d->streams[i])->running) {
      if (rey_endpoint_capture(i)) d->stats.capture_running = 1;
      else d->stats.render_running = 1;
    }
}
NTSTATUS ReyCreateStream(WDFDEVICE device, ACXCIRCUIT circuit, ACXPIN pin, PACXSTREAM_INIT init,
                            ACXDATAFORMAT format, const GUID *mode, ACXOBJECTBAG arguments) {
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(device);
  REY_CIRCUIT_CONTEXT *c = ReyCircuitContext(circuit);
  BOOLEAN capture = c->capture;
  WAVEFORMATEXTENSIBLE *wave = (WAVEFORMATEXTENSIBLE *)AcxDataFormatGetWaveFormatExtensible(format);
  WDF_OBJECT_ATTRIBUTES attributes;
  ACX_STREAM_CALLBACKS callbacks;
  ACX_RT_STREAM_CALLBACKS realtime;
  ACXSTREAM stream = NULL;
  NTSTATUS status;
  ULONG channels = c->channels;
  BOOLEAN *creating = &d->stream_creating[c->slot];
  UNREFERENCED_PARAMETER(pin);
  UNREFERENCED_PARAMETER(arguments);
  if (!mode ||
      (!IsEqualGUID(mode, &AUDIO_SIGNALPROCESSINGMODE_RAW) &&
       !IsEqualGUID(mode, &AUDIO_SIGNALPROCESSINGMODE_DEFAULT) &&
       !IsEqualGUID(mode, &AUDIO_SIGNALPROCESSINGMODE_COMMUNICATIONS) &&
       !IsEqualGUID(mode, &AUDIO_SIGNALPROCESSINGMODE_MEDIA) &&
       !IsEqualGUID(mode, &AUDIO_SIGNALPROCESSINGMODE_MOVIE)) ||
      !wave || wave->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE || wave->Format.cbSize < 22 ||
      wave->Format.nChannels != channels || wave->Format.nSamplesPerSec != d->profile.rate ||
      wave->Format.wBitsPerSample != 32 || wave->Format.nBlockAlign != channels * 4 ||
      wave->Samples.wValidBitsPerSample != d->profile.valid_bits ||
      !IsEqualGUID(&wave->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
    return STATUS_NO_MATCH;
  WdfWaitLockAcquire(d->lock, NULL);
  if (d->removing || *creating || d->streams[c->slot]) {
    WdfWaitLockRelease(d->lock);
    return STATUS_DEVICE_BUSY;
  }
  *creating = TRUE;
  WdfWaitLockRelease(d->lock);
  // Reserve the stream slot, then perform ACX allocation without holding the
  // PCM exchange lock used by the other direction's already running stream.
  ACX_STREAM_CALLBACKS_INIT(&callbacks);
  callbacks.EvtAcxStreamPrepareHardware = ReyStreamPrepare;
  callbacks.EvtAcxStreamReleaseHardware = ReyStreamRelease;
  callbacks.EvtAcxStreamRun = ReyStreamRun;
  callbacks.EvtAcxStreamPause = ReyStreamPause;
  status = AcxStreamInitAssignAcxStreamCallbacks(init, &callbacks);
  if (!NT_SUCCESS(status))
    goto done;
  ACX_RT_STREAM_CALLBACKS_INIT(&realtime);
  realtime.EvtAcxStreamAllocateRtPackets = ReyAllocatePackets;
  realtime.EvtAcxStreamFreeRtPackets = ReyFreePackets;
  realtime.EvtAcxStreamGetHwLatency = ReyStreamLatency;
  realtime.EvtAcxStreamGetCurrentPacket = ReyGetCurrentPacket;
  realtime.EvtAcxStreamGetPresentationPosition = ReyGetPosition;
  if (capture)
    realtime.EvtAcxStreamGetCapturePacket = ReyGetCapturePacket;
  else
    realtime.EvtAcxStreamSetRenderPacket = ReySetRenderPacket;
  status = AcxStreamInitAssignAcxRtStreamCallbacks(init, &realtime);
  if (!NT_SUCCESS(status))
    goto done;
  AcxStreamInitSetAcxRtStreamSupportsNotifications(init);
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, REY_STREAM_CONTEXT);
  attributes.EvtDestroyCallback = ReyStreamDestroy;
  status = AcxRtStreamCreate(device, circuit, &attributes, &init, &stream);
  if (NT_SUCCESS(status)) {
    REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
    s->device = device;
    s->slot = c->slot;
    s->first_channel = c->first_channel;
    s->capture = capture;
    s->channels = channels;
  }
done:
  WdfWaitLockAcquire(d->lock, NULL);
  *creating = FALSE;
  if (NT_SUCCESS(status)) {
    if (d->removing)
      status = STATUS_DEVICE_NOT_CONNECTED;
    else
      d->streams[c->slot] = stream;
  }
  WdfWaitLockRelease(d->lock);
  if (!NT_SUCCESS(status) && stream)
    WdfObjectDelete(stream);
  return status;
}
VOID ReyStreamDestroy(WDFOBJECT object) {
  REY_STREAM_CONTEXT *s = ReyStreamContext((ACXSTREAM)object);
  if (s->device) {
    REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
    WdfWaitLockAcquire(d->lock, NULL);
    SetRunning((ACXSTREAM)object, FALSE);
    if (d->streams[s->slot] == (ACXSTREAM)object)
      d->streams[s->slot] = NULL;
    WdfWaitLockRelease(d->lock);
    if (s->packets)
      ReyFreePackets((ACXSTREAM)object, s->packets, s->packet_count);
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
  ExFreePoolWithTag(packets, REY_POOL_TAG);
}
NTSTATUS ReyAllocatePackets(ACXSTREAM stream, ULONG count, ULONG bytes, PACX_RTPACKET *result) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
  PACX_RTPACKET packets = NULL;
  PVOID buffers[2] = {NULL, NULL};
  ULONG pages, i;
  PHYSICAL_ADDRESS low = {0}, high, skip = {0};
  NTSTATUS status = STATUS_SUCCESS;
  if (!count || count > 2 || !bytes || (count == 1 && bytes % PAGE_SIZE) ||
      bytes % (s->channels * 4) || bytes / (s->channels * 4) < d->profile.block ||
      bytes / (s->channels * 4) > REY_BRIDGE_PACKET_FRAMES ||
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
                                           REY_POOL_TAG);
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
VOID ReyFreePackets(ACXSTREAM stream, PACX_RTPACKET packets, ULONG count) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
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
NTSTATUS ReyStreamPrepare(ACXSTREAM stream) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
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
NTSTATUS ReyStreamRelease(ACXSTREAM stream) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  s->prepared = FALSE;
  SetRunning(stream, FALSE);
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
NTSTATUS ReyStreamRun(ACXSTREAM stream) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
  NTSTATUS status = STATUS_SUCCESS;
  WdfWaitLockAcquire(d->lock, NULL);
  if (!s->prepared || !s->packets || d->removing ||
      d->streams[s->slot] != stream)
    status = STATUS_DEVICE_NOT_READY;
  else {
    SetRunning(stream, TRUE);
  }
  WdfWaitLockRelease(d->lock);
  return status;
}
NTSTATUS ReyStreamPause(ACXSTREAM stream) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  SetRunning(stream, FALSE);
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
NTSTATUS ReyStreamLatency(ACXSTREAM stream, ULONG *fifo, ULONG *delay) {
  UNREFERENCED_PARAMETER(stream);
  *fifo = 0;
  *delay = 0;
  return STATUS_SUCCESS;
}
NTSTATUS ReySetRenderPacket(ACXSTREAM stream, ULONG packet, ULONG flags, ULONG eos_length) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
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
NTSTATUS ReyGetCurrentPacket(ACXSTREAM stream, ULONG *packet) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  // current_packet owns the next/incomplete buffer. Before its first frame,
  // the last completed packet is ULONG_MAX, so the OS may prefill packet zero.
  *packet = s->partial_frames ? s->current_packet : s->current_packet - 1;
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
NTSTATUS ReyGetCapturePacket(ACXSTREAM stream, ULONG *packet, ULONGLONG *qpc, BOOLEAN *more) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
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
NTSTATUS ReyGetPosition(ACXSTREAM stream, ULONGLONG *position, ULONGLONG *qpc) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(s->device);
  WdfWaitLockAcquire(d->lock, NULL);
  *position = s->position;
  *qpc = s->qpc;
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}

static VOID AdvanceStream(ACXSTREAM stream, const int32_t *input, int32_t *output, ULONG frames,
                          ULONGLONG qpc, REY_DEVICE_CONTEXT *d) {
  REY_STREAM_CONTEXT *s = ReyStreamContext(stream);
  REY_BRIDGE_STATS *stats = &d->stats;
  ULONG offset = 0;
  while (offset < frames) {
    ULONG available = s->packet_frames - s->partial_frames, chunk = min(available, frames - offset);
    ULONG index = s->current_packet % s->packet_count;
    PBYTE packet = PacketBuffer(s, s->current_packet) + s->partial_frames * s->channels * 4;
    if (s->capture) {
      if (!s->partial_frames && s->current_packet - s->reported_packet >= s->packet_count)
        ++stats->capture_overruns;
      rey_endpoint_capture_pcm((int32_t *)packet,
          input ? input + (SIZE_T)offset * d->profile.inputs : NULL,
          chunk, s->channels, d->profile.inputs, s->first_channel);
      if (!s->partial_frames)
        s->last_capture_qpc = qpc;
      stats->capture_frames += chunk;
    } else {
      BOOLEAN ready = s->render_valid[index] && s->render_ready[index] == s->current_packet;
      ULONG length = ready && s->render_length[index] > s->partial_frames
                         ? min(chunk, s->render_length[index] - s->partial_frames)
                         : 0;
      if (length && output)
        (VOID)rey_endpoint_render_pcm(output + (SIZE_T)offset * d->profile.outputs,
            (const int32_t *)packet, length, s->channels, d->profile.outputs,
            s->first_channel, d->profile.valid_bits);
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
NTSTATUS ReyExchange(WDFDEVICE child, const REY_BRIDGE_EXCHANGE *input,
                        REY_BRIDGE_EXCHANGE *output) {
  REY_DEVICE_CONTEXT *d = ReyDeviceContext(child);
  ULONG frames = input->frames, flags = 0, slot;
  uint32_t gap = 0;
  ULONGLONG qpc = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
  if (!rey_bridge_valid_exchange(input, &d->profile))
    return STATUS_INVALID_PARAMETER;
  WdfWaitLockAcquire(d->lock, NULL);
  if (d->removing) {
    WdfWaitLockRelease(d->lock);
    return STATUS_DEVICE_NOT_CONNECTED;
  }
  if (d->bridge_seen &&
      (input->epoch != d->bridge_epoch ||
       !rey_bridge_frame_gap(input, d->next_bridge_frame, d->profile.block * 4, &gap))) {
    WdfWaitLockRelease(d->lock);
    return STATUS_DATA_ERROR;
  }
  d->bridge_seen = TRUE;
  d->bridge_epoch = (USHORT)input->epoch;
  d->next_bridge_frame = input->frame_position + frames;
  if (input->flags)
    ++d->stats.discontinuities;
  for (slot = 0; slot < REY_ENDPOINT_SLOTS; slot += 2)
    if (d->streams[slot] && ReyStreamContext(d->streams[slot])->running) {
      if (gap) AdvanceStream(d->streams[slot], NULL, NULL, gap, qpc, d);
      AdvanceStream(d->streams[slot], input->samples, NULL, frames, qpc, d);
      flags |= REY_BRIDGE_CAPTURE_ACTIVE;
    }
  // Both METHOD_BUFFERED directions may alias. Capture has consumed the input.
  RtlZeroMemory(output->samples, sizeof(output->samples));
  for (slot = 1; slot < REY_ENDPOINT_SLOTS; slot += 2)
    if (d->streams[slot] && ReyStreamContext(d->streams[slot])->running) {
      if (gap) AdvanceStream(d->streams[slot], NULL, NULL, gap, qpc, d);
      AdvanceStream(d->streams[slot], NULL, output->samples, frames, qpc, d);
      flags |= REY_BRIDGE_RENDER_ACTIVE;
    }
  output->flags = flags;
  output->qpc = qpc;
  ++d->stats.exchanges;
  WdfWaitLockRelease(d->lock);
  return STATUS_SUCCESS;
}
