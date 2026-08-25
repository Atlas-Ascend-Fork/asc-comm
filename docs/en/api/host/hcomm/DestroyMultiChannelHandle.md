# DestroyMultiChannelHandle

## Function Description

Destroys the shared-Jetty channels and Device context created by `MakeMultiChannelHandle`. The API calls `HcommChannelDestroy` for every owned channel, then calls `aclrtFree` to release the multi-channel metadata.

## Function Prototype

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `multiChannel` | Input | Valid handle returned by `MakeMultiChannelHandle`. |

## Return Value

Returns `HCCL_SUCCESS` on success or the corresponding Hcomm error code on failure. The API attempts to release the Device context even if channel destruction fails.

## Constraints

- Ensure that all Kernels using the handle have completed and synchronized before calling this API.
- Destroy each handle only once and do not use it after destruction.
- Do not pass a communicator-managed handle returned by `MakeMultiChannelHandleWithComm`.
- Call this API before `HcommEndpointDestroy`.

## Related APIs

- [MakeMultiChannelHandle](MakeMultiChannelHandle.md)
