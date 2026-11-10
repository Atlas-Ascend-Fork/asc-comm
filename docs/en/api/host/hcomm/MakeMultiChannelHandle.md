# MakeMultiChannelHandle

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-05T01:46:55.850Z pushedAt=2026-11-09T06:49:22.792Z nl=2 -->

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

Creates the communication resources required for the multi-channel mode of batch APIs and returns the `MultiChannelHandle` used on the device side. The API provides two overloads: the HCCL (Huawei Collective Communication Library) communicator overload and the Hcomm endpoint overload (an independent communication endpoint component that does not depend on the HCCL communicator, provided by the external hcomm component).

## Function Prototype

HCCL communicator overload:

```cpp
HcclResult MakeMultiChannelHandle(
    HcclComm comm,
    const char* sharedQueueTag,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

Endpoint overload:

```cpp
HcommResult MakeMultiChannelHandle(
    EndpointHandle endpointHandle,
    HcommChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `comm` | Input | Initialized HCCL communicator. |
| `sharedQueueTag` | Input | Tag of this group of multi-channel resources, which must be unique within the same communicator. |
| `endpointHandle` | Input | Initialized Hcomm endpoint that supports the `COMM_PROTOCOL_UB_CTP` protocol of the AIV (AI Vector) engine. |
| `channelDescs` | Input | Array of HCCL or Hcomm channel descriptions containing `channelNum` elements. See [HcclChannelDesc](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/HcclChannelDesc.md) and [HcommChannelDesc](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/HcommChannelDesc.md). The array subscript corresponds to the `channelIndex` of [GetHandleRef](../../aicore/hcomm/GetHandleRef.md) on the device side. |
| `channelNum` | Input | Number of channel descriptions, which must be greater than 0. |
| `multiChannel` | Output | Multi-channel handle on the device side, which must point to a valid writable address (cannot be `nullptr`). Set this parameter to **0** on creation failure. |

## Return Value

The HCCL communicator overload returns `HCCL_SUCCESS` on success and the corresponding HCCL error code on failure. The endpoint overload returns `HCCL_SUCCESS` on success and the corresponding Hcomm error code on failure. The main error codes and their causes are as follows:

**Table 2** Return error codes

| Error Code | Cause |
| --- | --- |
| `HCCL_E_PARA` | Invalid parameter: `multiChannel`, `comm`, or `endpointHandle` is a null pointer, `sharedQueueTag` or `channelDescs` is null, or `channelNum` is 0. |
| `HCCL_E_MEMORY` | Host-side memory allocation failed. |
| `HCCL_E_TIMEOUT` | The endpoint overload timed out waiting for the channel to become ready (failed after retrying for about 120 seconds). |
| `HCCL_E_INTERNAL` | Channel connection failed or the internal state is abnormal (possible for both overloads). |
| `HCCL_E_RUNTIME` | ACL runtime call failed. |
| Other HCCL error codes | Propagated from internal calls such as HCCL channel acquisition and channel configuration (possible for both overloads). |

## Usage Constraints

- All input pointers must not be `nullptr`, and `channelNum` must be greater than 0.
- The HCCL communicator overload requires `comm` to be initialized and to remain valid while the kernel uses the `multiChannel` handle. Within the same communicator, each `sharedQueueTag` can be used for only one successful call.
- In the communicator overload, the channel description protocol (`HcclChannelDesc.channelProtocol`) must be consistent with the `AscendC::Hcomm` template protocol on the kernel side. For details, see [CommProtocol](../../aicore/hcomm/CommProtocol.md).
- Resources created by the HCCL communicator overload are managed by `comm` and are automatically released when the communicator is destroyed. Do not call [DestroyMultiChannelHandle](./DestroyMultiChannelHandle.md) to release them.
- The endpoint overload requires `endpointHandle` to be initialized and to support the `COMM_ENGINE_AIV` and `COMM_PROTOCOL_UB_CTP` protocols.
- Resources created by the endpoint overload are managed by the caller. After the kernel that uses this handle finishes execution and synchronization, you must call `DestroyMultiChannelHandle` before destroying `endpointHandle`.
- The created multi-channel resources cannot be used concurrently.
- The array subscript of `channelDescs` on the host side must be consistent with the `channelIndex` passed to `GetHandleRef` on the device side.

## Call Example

The following is an example of calling the **MakeMultiChannelHandle** API. For a complete example of the HCCL communicator overload, see [02_hcomm_urma_batch_write](../../../../../examples/aicore/hcomm/02_hcomm_urma_batch_write/README.md).

HCCL communicator overload:

```cpp
// comm is an initialized HCCL communicator, and channelDescs is a filled channel description array.
HcclResult MakeChannelsByComm(
    HcclComm comm, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    // sharedQueueTag must be unique within the communicator to which this group of multi-channel resources belongs.
    const char* sharedQueueTag = "hcomm_shared_queue_0";

    // Create multi-channel resources and return the multi-channel handle used on the device side.
    // The resources created by this overload are managed by comm and are automatically released when the communicator is destroyed. Do not call DestroyMultiChannelHandle.
    return MakeMultiChannelHandle(
        comm, sharedQueueTag, channelDescs, channelNum, multiChannel);
}
```

Endpoint overload:

```cpp
// endpointHandle is an initialized endpoint that supports the AIV engine COMM_PROTOCOL_UB_CTP protocol, and channelDescs is an array of populated channel descriptions.
HcommResult MakeChannelsByEndpoint(
    EndpointHandle endpointHandle, HcommChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    // Create multi-channel resources and return the multi-channel handle used on the device side.
    // The resources created by this overload are managed by the caller. After the kernel finishes execution and synchronization, call DestroyMultiChannelHandle to release them.
    return MakeMultiChannelHandle(
        endpointHandle, channelDescs, channelNum, multiChannel);
}
```
