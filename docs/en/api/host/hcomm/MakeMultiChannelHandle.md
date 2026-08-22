# MakeMultiChannelHandle

## Function Description

Creates a group of UBC_CTP communication channels that share one Jetty on the Host and creates the `MultiChannelHandle` used on the Device.

The interface stores per-channel remote communication information in the order of `channelDescs`. Pass the returned handle to an AICore Kernel and use `MakeBatchHandle` to create a multi-channel batch handle.

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
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `comm` | Input | Initialized HCCL communicator. The shared-Jetty channels and Device context created by this interface are managed by this communicator. |
| `sharedQueueTag` | Input | Shared queue tag. Within one communicator, this tag identifies the shared-Jetty channel group and its Device context. |
| `channelDescs` | Input | Array containing `channelNum` channel descriptors. The array order determines the `channelIndex` used by `GetHandleRef`. |
| `channelNum` | Input | Number of channel descriptors. It must be greater than 0. |
| `multiChannel` | Output | Device-side multi-channel handle. Its underlying value is the address of a Device context managed by the communicator. |

## Return Value

| Return Value | Description |
| --- | --- |
| `HCCL_SUCCESS` | Creation succeeds and `multiChannel` receives a valid handle. |
| Other value | Creation fails. When `multiChannel` is not null, it is set to 0, and the corresponding HCCL error code is returned. |

## Constraints

- Input pointers must not be `nullptr`, and `channelNum` must not be 0.
- Channels described by `channelDescs` must use `COMM_ENGINE_AIV` and `COMM_PROTOCOL_UBC_CTP` and satisfy HCCL shared-queue channel constraints.
- Within one communicator, each `sharedQueueTag` can be used by only one successful `MakeMultiChannelHandle` call. Repeated creation returns an error.
- The returned handle and its Device context are managed by `comm` and require no separate destruction. Do not destroy the communicator before Kernels using the handle have completed.

## Related APIs

- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
