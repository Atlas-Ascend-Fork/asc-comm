# DestroyMultiChannelHandleWithComm

## Function Description

Releases the Device-side multi-channel context created by `MakeMultiChannelHandleWithComm`. The shared-Jetty channels remain managed by the HCCL communicator.

Header file:

```cpp
#include "hcomm/hcomm_host.h"
```

## Function Prototype

```cpp
HcclResult DestroyMultiChannelHandleWithComm(HcclComm comm, const char* sharedQueueTag);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `comm` | Input | HCCL communicator used to create the multi-channel handle. |
| `sharedQueueTag` | Input | Shared queue tag used to create the multi-channel handle. |

## Return Value

Returns `HCCL_SUCCESS` on success or the corresponding HCCL error code on failure.

## Constraints

- All Kernels using the multi-channel handle must have completed and synchronized before this call. The corresponding `MultiChannelHandle` becomes invalid after the call.
- The API releases the Device context through `HcclEngineCtxDestroy` and does not destroy shared channels. Shared channels are released with `comm`.
- Use this API only for a context created by `MakeMultiChannelHandleWithComm`. Use `DestroyMultiChannelHandle` for the Endpoint path.

## Related APIs

- [MakeMultiChannelHandleWithComm](MakeMultiChannelHandleWithComm.md)
- [DestroyMultiChannelHandle](DestroyMultiChannelHandle.md)
