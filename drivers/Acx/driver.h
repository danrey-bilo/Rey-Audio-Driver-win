#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <acx.h>
#include <windef.h>
#include <ks.h>
#include <mmsystem.h>
#include <ksmedia.h>
#include "../../include/rey/bridge.h"
#include "../../include/rey/endpoints.h"

#define REY_POOL_TAG 'piaP'
extern const GUID GUID_REY_CONTROL;
extern const GUID GUID_REY_CAPTURE;
extern const GUID GUID_REY_RENDER;
extern const GUID GUID_REY_PAIR_CAPTURE;
extern const GUID GUID_REY_PAIR_RENDER;

typedef struct REY_CHILD_SLOT {
  WDFDEVICE child;
  WDFFILEOBJECT owner;
  BOOLEAN removing;
  char device_id[32];
} REY_CHILD_SLOT;

typedef struct REY_FILE_CONTEXT {
  WDFDEVICE root;
  ULONG slot;
  BOOLEAN closing;
} REY_FILE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REY_FILE_CONTEXT, ReyFileContext);

typedef struct REY_DEVICE_CONTEXT {
  WDFDEVICE root;
  WDFFILEOBJECT owner;
  ULONG device_slot;
  REY_CHILD_SLOT children[REY_DEVICE_LIMIT];
  WDFWAITLOCK lock;
  BOOLEAN is_child, removing, circuits_added;
  BOOLEAN stream_creating[REY_ENDPOINT_SLOTS];
  REY_BRIDGE_PROFILE profile;
  ACXCIRCUIT circuits[REY_ENDPOINT_SLOTS];
  ACXSTREAM streams[REY_ENDPOINT_SLOTS];
  REY_BRIDGE_STATS stats;
  ULONGLONG next_bridge_frame;
  USHORT bridge_epoch;
  BOOLEAN bridge_seen;
} REY_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REY_DEVICE_CONTEXT, ReyDeviceContext);

typedef struct REY_CIRCUIT_CONTEXT {
  BOOLEAN capture;
  ULONG slot, channels, first_channel;
} REY_CIRCUIT_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REY_CIRCUIT_CONTEXT, ReyCircuitContext);

typedef struct REY_STREAM_CONTEXT {
  WDFDEVICE device;
  ULONG slot, first_channel;
  BOOLEAN capture, running, prepared, allocating;
  PACX_RTPACKET packets;
  PVOID buffers[2];
  ULONG packet_count, packet_bytes, packet_frames, channels;
  ULONG current_packet, reported_packet, partial_frames;
  ULONG render_ready[2], render_length[2];
  BOOLEAN render_valid[2];
  ULONGLONG position, qpc, last_capture_qpc;
} REY_STREAM_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REY_STREAM_CONTEXT, ReyStreamContext);

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD ReyDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE ReyPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE ReyReleaseHardware;
EVT_WDF_OBJECT_CONTEXT_CLEANUP ReyChildCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ReyIoControl;
EVT_WDF_FILE_CLEANUP ReyFileCleanup;
EVT_WDF_DEVICE_FILE_CREATE ReyFileCreate;
EVT_ACX_CIRCUIT_CREATE_STREAM ReyCreateStream;
EVT_ACX_CIRCUIT_COMPOSITE_CIRCUIT_INITIALIZE ReyCircuitInitialize;
EVT_WDF_OBJECT_CONTEXT_DESTROY ReyStreamDestroy;
EVT_ACX_STREAM_ALLOCATE_RTPACKETS ReyAllocatePackets;
EVT_ACX_STREAM_FREE_RTPACKETS ReyFreePackets;
EVT_ACX_STREAM_PREPARE_HARDWARE ReyStreamPrepare;
EVT_ACX_STREAM_RELEASE_HARDWARE ReyStreamRelease;
EVT_ACX_STREAM_RUN ReyStreamRun;
EVT_ACX_STREAM_PAUSE ReyStreamPause;
EVT_ACX_STREAM_GET_HW_LATENCY ReyStreamLatency;
EVT_ACX_STREAM_SET_RENDER_PACKET ReySetRenderPacket;
EVT_ACX_STREAM_GET_CURRENT_PACKET ReyGetCurrentPacket;
EVT_ACX_STREAM_GET_CAPTURE_PACKET ReyGetCapturePacket;
EVT_ACX_STREAM_GET_PRESENTATION_POSITION ReyGetPosition;

NTSTATUS ReyCreateChild(WDFDEVICE root, WDFFILEOBJECT file, const REY_BRIDGE_PROFILE *profile);
NTSTATUS ReyDetachChild(WDFFILEOBJECT file);
WDFDEVICE ReyReferenceChild(WDFFILEOBJECT file);
NTSTATUS ReyCreateCircuit(WDFDEVICE device, ULONG slot, ACXCIRCUIT *result);
NTSTATUS ReyExchange(WDFDEVICE child, const REY_BRIDGE_EXCHANGE *input,
                        REY_BRIDGE_EXCHANGE *output);
