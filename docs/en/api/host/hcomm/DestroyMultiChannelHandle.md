# DestroyMultiChannelHandle

## Function Description

This is the destroy API corresponding to the `MakeMultiChannelHandle` overload without an HCCL communicator (the `EndpointHandle` overload). It destroys the shared-Jetty channels and Device context by calling `HcommChannelDestroy` for all channels and then calling `aclrtFree` for the multi-channel metadata.

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

Returns `HCCL_SUCCESS` on success or the corresponding Hcomm error code on failure. The API attempts to release the Device context even if channel destruction fails.

## Constraints

- Ensure that all Kernels using the handle have completed and synchronized before calling this API.
- Use this API only for a handle created by the overload without an HCCL communicator and call it before `HcommEndpointDestroy`. Resources created by the `HcclComm` overload are released automatically with the communicator and must not be passed to this API.
- Destroy each multi-channel handle only once and do not use it after destruction.

## Related APIs

- [MakeMultiChannelHandle](MakeMultiChannelHandle.md)
