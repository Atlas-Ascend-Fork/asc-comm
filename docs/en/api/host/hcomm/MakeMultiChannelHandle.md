# MakeMultiChannelHandle

## Function Description

Creates a group of shared-Jetty UB_CTP channels from an Hcomm Endpoint and creates the `MultiChannelHandle` used on the Device. The API calls `HcommChannelCreateWithConfig`, waits until every channel is ready, and stores remote communication metadata in `channelDescs` order.

Header file:

```cpp
#include "hcomm/hcomm_host.h"
```

## Function Prototype

```cpp
HcommResult MakeMultiChannelHandle(
    EndpointHandle endpointHandle,
    HcommChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `endpointHandle` | Input | Initialized Hcomm Endpoint that supports the AIV engine and UB_CTP protocol. |
| `channelDescs` | Input | Hcomm channel descriptor array containing `channelNum` elements. Array order determines the `channelIndex` used by `GetHandleRef`. |
| `channelNum` | Input | Number of channel descriptors. It must be greater than 0. |
| `multiChannel` | Output | Device-side multi-channel handle. It is set to 0 when creation fails. |

## Return Value

Returns `HCCL_SUCCESS` on success or the corresponding Hcomm error code on failure. If connection establishment fails, resources are unavailable, or all channels are not ready within 120 seconds, the API destroys the channels it created and returns an error.

## Constraints

- Input pointers must not be `nullptr`, and `channelNum` must not be 0.
- Channels use `COMM_ENGINE_AIV` and UB_CTP and follow the shared-Jetty constraints of `HcommChannelCreateWithConfig`.
- Channels with the same source and destination Endpoint Pair under one Endpoint share a Jetty. Shared-Jetty channels cannot be used concurrently.
- The returned handle owns the created channels and Device context. After the Kernel has completed and synchronized, call `DestroyMultiChannelHandle` before destroying `endpointHandle`.
- Use `MakeMultiChannelHandleWithComm` for the communicator-owned path.

## Related APIs

- [DestroyMultiChannelHandle](DestroyMultiChannelHandle.md)
- [MakeMultiChannelHandleWithComm](MakeMultiChannelHandleWithComm.md)
- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
