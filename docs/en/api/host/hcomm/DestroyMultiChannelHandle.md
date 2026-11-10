# DestroyMultiChannelHandle

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-05T01:46:11.991Z pushedAt=2026-11-09T06:49:22.790Z nl=2 -->

## Supported Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference products: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training products: Not supported
<!-- end id6 -->

## Function Description

Header file path: `"hcomm/hcomm_host.h"`.

Releases the multi-channel resources created by the endpoint overload of [MakeMultiChannelHandle](./MakeMultiChannelHandle.md).

## Function Prototype

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `multiChannel` | Input | Valid handle returned by the endpoint overload of `MakeMultiChannelHandle`. |

## Return Value

Returns `HCCL_SUCCESS` on success; otherwise, returns the corresponding Hcomm error code. The main error codes and their causes are as follows:

**Table 2** Return error codes

| Error Code | Cause |
| --- | --- |
| `HCCL_E_PTR` | `multiChannel` is 0 (invalid handle). |
| `HCCL_E_PARA` | The number of channels in the handle entity is invalid. |
| `HCCL_E_RUNTIME` | An ACL runtime call fails when copying the handle entity from the device or releasing resources. |
| `HCCL_E_INTERNAL` | Field validation of the multi-channel handle entity fails. |
| `HCCL_E_MEMORY` | Temporary memory allocation on the host side fails. |

## Usage Constraints

- Before calling this API, ensure that the kernel using this handle has completed execution and has been synchronized.
- This API can only be used for handles created by the endpoint overload, and it must be called before `HcommEndpointDestroy`. Resources created by the HCCL (Huawei Collective Communication Library) communicator overload are automatically released by the communicator and cannot be passed to this API.
- Each multi-channel handle can be destroyed only once and cannot be used after destruction.

## Call Example

The following is an example of calling the **DestroyMultiChannelHandle** API.

```cpp
// Prerequisite: endpointHandle is an initialized Hcomm endpoint, and multiChannel is the multi-channel handle created by its MakeMultiChannelHandle overload.
// HcommEndpointDestroy is the matching destruction API for the Hcomm endpoint and returns HcommResult.
HcommResult DestroyChannels(EndpointHandle endpointHandle, MultiChannelHandle multiChannel)
{
    // After the kernel using multiChannel finishes execution and is synchronized, destroy the multi-channel resources.
    HcommResult ret = DestroyMultiChannelHandle(multiChannel);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    // Only after the multi-channel resources are destroyed can the Hcomm endpoint that created them be destroyed.
    return HcommEndpointDestroy(endpointHandle);
}
```
