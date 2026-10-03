#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <acx.h>
#include <windef.h>
#include <ks.h>
#include <mmsystem.h>
#include <ksmedia.h>
#include "../../include/piaoip/bridge.h"

#define PIAOIP_POOL_TAG 'piaP'
extern const GUID GUID_PIAOIP_CONTROL;
extern const GUID GUID_PIAOIP_CAPTURE;
extern const GUID GUID_PIAOIP_RENDER;

typedef struct PIAOIP_DEVICE_CONTEXT {
  WDFDEVICE root, child;
  WDFWAITLOCK lock;
  BOOLEAN is_child, removing, child_removing, circuits_added;
  BOOLEAN capture_creating, render_creating;
  PIAOIP_BRIDGE_PROFILE profile;
  ACXCIRCUIT capture, render;
  ACXSTREAM capture_stream, render_stream;
  PIAOIP_BRIDGE_STATS stats;
  ULONGLONG next_bridge_frame;
  USHORT bridge_epoch;
  BOOLEAN bridge_seen;
} PIAOIP_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIAOIP_DEVICE_CONTEXT, PiaoipDeviceContext);

typedef struct PIAOIP_CIRCUIT_CONTEXT {
  BOOLEAN capture;
} PIAOIP_CIRCUIT_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIAOIP_CIRCUIT_CONTEXT, PiaoipCircuitContext);

typedef struct PIAOIP_STREAM_CONTEXT {
  WDFDEVICE device;
  BOOLEAN capture, running, prepared, allocating;
  PACX_RTPACKET packets;
  PVOID buffers[2];
  ULONG packet_count, packet_bytes, packet_frames, channels;
  ULONG current_packet, reported_packet, partial_frames;
  ULONG render_ready[2], render_length[2];
  BOOLEAN render_valid[2];
  ULONGLONG position, qpc, last_capture_qpc;
} PIAOIP_STREAM_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PIAOIP_STREAM_CONTEXT, PiaoipStreamContext);

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD PiaoipDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE PiaoipPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE PiaoipReleaseHardware;
EVT_WDF_OBJECT_CONTEXT_CLEANUP PiaoipChildCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL PiaoipIoControl;
EVT_WDF_FILE_CLEANUP PiaoipFileCleanup;
EVT_ACX_CIRCUIT_CREATE_STREAM PiaoipCreateStream;
EVT_ACX_CIRCUIT_COMPOSITE_CIRCUIT_INITIALIZE PiaoipCircuitInitialize;
EVT_WDF_OBJECT_CONTEXT_DESTROY PiaoipStreamDestroy;
EVT_ACX_STREAM_ALLOCATE_RTPACKETS PiaoipAllocatePackets;
EVT_ACX_STREAM_FREE_RTPACKETS PiaoipFreePackets;
EVT_ACX_STREAM_PREPARE_HARDWARE PiaoipStreamPrepare;
EVT_ACX_STREAM_RELEASE_HARDWARE PiaoipStreamRelease;
EVT_ACX_STREAM_RUN PiaoipStreamRun;
EVT_ACX_STREAM_PAUSE PiaoipStreamPause;
EVT_ACX_STREAM_GET_HW_LATENCY PiaoipStreamLatency;
EVT_ACX_STREAM_SET_RENDER_PACKET PiaoipSetRenderPacket;
EVT_ACX_STREAM_GET_CURRENT_PACKET PiaoipGetCurrentPacket;
EVT_ACX_STREAM_GET_CAPTURE_PACKET PiaoipGetCapturePacket;
EVT_ACX_STREAM_GET_PRESENTATION_POSITION PiaoipGetPosition;

NTSTATUS PiaoipCreateChild(WDFDEVICE root, const PIAOIP_BRIDGE_PROFILE *profile);
NTSTATUS PiaoipDetachChild(WDFDEVICE root);
NTSTATUS PiaoipCreateCircuit(WDFDEVICE device, BOOLEAN capture, ACXCIRCUIT *result);
NTSTATUS PiaoipExchange(WDFDEVICE child, const PIAOIP_BRIDGE_EXCHANGE *input,
                        PIAOIP_BRIDGE_EXCHANGE *output);
