# MakeMultiChannelHandle

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->

## Function Description

Creates the communication resources required by the multi-channel batch workflow and returns the `MultiChannelHandle` used on the Device. The API provides overloads for an HCCL communicator and an Hcomm Endpoint.

Header file:

```cpp
#include "hcomm/hcomm_host.h"
```

## Function Prototype

```cpp
HcclResult MakeMultiChannelHandle(
    HcclComm comm,
    const char* sharedQueueTag,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);

HcommResult MakeMultiChannelHandle(
    EndpointHandle endpointHandle,
    HcommChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

## Parameter Description

**Table 1**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `comm` | Input | Initialized HCCL communicator. |
| `sharedQueueTag` | Input | Tag for this group of multi-channel resources. It must be unique within the communicator. |
| `endpointHandle` | Input | Initialized Hcomm Endpoint that supports the AIV engine and UB_CTP protocol. |
| `channelDescs` | Input | HCCL or Hcomm channel descriptor array containing `channelNum` elements. Each array index corresponds to the `channelIndex` passed to Device-side `GetHandleRef`. |
| `channelNum` | Input | Number of channel descriptors. Must be greater than 0. |
| `multiChannel` | Output | Device-side multi-channel handle. Set to 0 if creation fails. |

## Return Value

The HCCL communicator overload returns `HCCL_SUCCESS` on success or the corresponding HCCL error code on failure. The Hcomm Endpoint overload returns the success code or the corresponding Hcomm error code.

## Constraints

- All input pointers must be non-null, and `channelNum` must be greater than 0.
- The communicator overload requires an initialized `comm`. Within one communicator, each `sharedQueueTag` can be used by only one successful call.
- Resources created by the communicator overload are managed by `comm` and released automatically when the communicator is destroyed. Do not pass this handle to `DestroyMultiChannelHandle`.
- The Endpoint overload requires an initialized `endpointHandle` that supports `COMM_ENGINE_AIV` and UB_CTP.
- Resources created by the Endpoint overload are managed by the caller. After all Kernels using the handle have completed and synchronized, call `DestroyMultiChannelHandle` before destroying `endpointHandle`.
- Do not use the created multi-channel resources concurrently.
- Keep each Host-side index in `channelDescs` consistent with the `channelIndex` passed to Device-side `GetHandleRef`.

## Example

Create with an HCCL communicator:

```cpp
HcclComm comm; // Initialized HCCL communicator
const char* sharedQueueTag = "batch_channels";
HcclChannelDesc channelDescs[2]; // Initialized with peer communication information
uint32_t channelNum = 2U;
MultiChannelHandle multiChannel = 0U;

HcclResult ret = MakeMultiChannelHandle(
    comm, sharedQueueTag, channelDescs, channelNum, &multiChannel);
```

Create with an Hcomm Endpoint:

```cpp
EndpointHandle endpointHandle; // Initialized with AIV-engine UB_CTP support
HcommChannelDesc channelDescs[2]; // Initialized with peer communication information
uint32_t channelNum = 2U;
MultiChannelHandle multiChannel = 0U;

HcommResult ret = MakeMultiChannelHandle(
    endpointHandle, channelDescs, channelNum, &multiChannel);
```
