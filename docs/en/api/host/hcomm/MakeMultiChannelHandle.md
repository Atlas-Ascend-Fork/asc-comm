# MakeMultiChannelHandle

## Function Description

Creates a group of shared-Jetty UB_CTP channels and the `MultiChannelHandle` used on the Device. The API provides overloads for an HCCL communicator and an Hcomm Endpoint and stores remote communication metadata in `channelDescs` order.

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

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `comm` | Input | Initialized HCCL communicator that manages the shared-Jetty channels and Device context. |
| `sharedQueueTag` | Input | Tag for this shared queue and Device context. It must be unique within the communicator. |
| `endpointHandle` | Input | Initialized Hcomm Endpoint that supports the AIV engine and UB_CTP protocol. |
| `channelDescs` | Input | HCCL or Hcomm channel descriptor array containing `channelNum` elements. Array order determines the `channelIndex` used by `GetHandleRef`. |
| `channelNum` | Input | Number of channel descriptors. It must be greater than 0. |
| `multiChannel` | Output | Device-side multi-channel handle. It is set to 0 when creation fails. |

## Return Value

Returns `HCCL_SUCCESS` on success. The communicator overload returns an HCCL error code on failure, and the Endpoint overload returns an Hcomm error code. If Endpoint connection establishment fails, resources are unavailable, or all channels are not ready within 120 seconds, the API destroys the channels it created and returns an error.

## Constraints

- Input pointers for both overloads must not be `nullptr`, and `channelNum` must not be 0.
- The communicator overload creates channels through `HcclChannelAcquireWithConfig`. Each `sharedQueueTag` can be used by only one successful call within a communicator. The communicator manages and automatically releases the shared channels and Device context; do not call `DestroyMultiChannelHandle` for this handle.
- The Endpoint overload creates channels through `HcommChannelCreateWithConfig` and waits for them to become ready. The returned handle owns the channels and Device context and must be destroyed before `endpointHandle`.
- Channels use `COMM_ENGINE_AIV` and UB_CTP. Shared-Jetty channels cannot be used concurrently.
- After Kernels using a handle created by the Endpoint overload have completed and synchronized, call `DestroyMultiChannelHandle`.

## Related APIs

- [DestroyMultiChannelHandle](DestroyMultiChannelHandle.md)
- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
