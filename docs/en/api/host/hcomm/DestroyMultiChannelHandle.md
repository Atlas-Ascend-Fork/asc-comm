# DestroyMultiChannelHandle

## Function Description

Releases multi-channel resources created by the Hcomm Endpoint overload of `MakeMultiChannelHandle`.

Header file:

```cpp
#include "hcomm/hcomm_host.h"
```

## Function Prototype

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `multiChannel` | Input | Valid handle returned by the Endpoint overload of `MakeMultiChannelHandle`. |

## Return Value

Returns `HCCL_SUCCESS` on success or the corresponding Hcomm error code on failure.

## Constraints

- Ensure that all Kernels using the handle have completed and synchronized before calling this API.
- Use this API only for a handle created by the overload without an HCCL communicator and call it before `HcommEndpointDestroy`. Resources created by the `HcclComm` overload are released automatically with the communicator and must not be passed to this API.
- Destroy each multi-channel handle only once and do not use it after destruction.

## Related APIs

- [MakeMultiChannelHandle](MakeMultiChannelHandle.md)
