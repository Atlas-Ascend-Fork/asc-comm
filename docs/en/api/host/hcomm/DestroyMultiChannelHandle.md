# DestroyMultiChannelHandle

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

**Table 1**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `multiChannel` | Input | Valid handle returned by the Endpoint overload of `MakeMultiChannelHandle`. |

## Return Value

Returns `HCCL_SUCCESS` on success or the corresponding Hcomm error code on failure.

## Constraints

- Ensure that all Kernels using the handle have completed and synchronized before calling this API.
- Use this API only for a handle created by the overload without an HCCL communicator and call it before `HcommEndpointDestroy`. Resources created by the `HcclComm` overload are released automatically with the communicator and must not be passed to this API.
- Destroy each multi-channel handle only once and do not use it after destruction.

## Example

```cpp
MultiChannelHandle multiChannel; // Created by the Endpoint overload; related Kernels have completed and synchronized

HcommResult ret = DestroyMultiChannelHandle(multiChannel);
```
