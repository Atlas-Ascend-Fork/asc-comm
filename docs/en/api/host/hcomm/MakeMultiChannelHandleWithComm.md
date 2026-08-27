# MakeMultiChannelHandleWithComm

## Function Description

Creates a group of shared-Jetty UB_CTP channels from an HCCL communicator and creates the `MultiChannelHandle` used on the Device. The API stores remote communication metadata in `channelDescs` order.

Header file:

```cpp
#include "hcomm/hcomm_host.h"
```

## Function Prototype

```cpp
HcclResult MakeMultiChannelHandleWithComm(
    HcclComm comm,
    const char* sharedQueueTag,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `comm` | Input | Initialized HCCL communicator that manages the shared-Jetty channels and Device context. |
| `sharedQueueTag` | Input | Tag for this shared queue and Device context. It must be unique within the communicator. |
| `channelDescs` | Input | HCCL channel descriptor array containing `channelNum` elements. Array order determines the `channelIndex` used by `GetHandleRef`. |
| `channelNum` | Input | Number of channel descriptors. It must be greater than 0. |
| `multiChannel` | Output | Device-side multi-channel handle. It is set to 0 when creation fails. |

## Return Value

Returns `HCCL_SUCCESS` on success or the corresponding HCCL error code on failure.

## Constraints

- Channels use `COMM_ENGINE_AIV` and UB_CTP and satisfy the HCCL shared-queue channel constraints.
- Each `sharedQueueTag` can be used by only one successful call within a communicator.
- After Kernels using the handle have completed and synchronized, call `DestroyMultiChannelHandleWithComm` to release the Device context early. If it is not called, the context is released with `comm`. Shared channels always remain managed by `comm`.
- Use `MakeMultiChannelHandle` for the path without a communicator.

## Related APIs

- [MakeMultiChannelHandle](MakeMultiChannelHandle.md)
- [DestroyMultiChannelHandleWithComm](DestroyMultiChannelHandleWithComm.md)
- [MakeBatchHandle](../../aicore/hcomm/MakeBatchHandle.md)
- [GetHandleRef](../../aicore/hcomm/GetHandleRef.md)
