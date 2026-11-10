# notify_record

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:43:17.066Z pushedAt=2026-11-09T06:49:22.788Z nl=2 -->

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

Header file: `#include "ccu/hcomm/ccu_primitives.hpp"`

Sends a completion signal to the peer die through a channel within the CCU kernel, setting the notify flag corresponding to `mask` in the peer synchronization register to 1 so that the peer's [notify_wait](notify_wait.md) can read the completion signal.

> [!NOTE] Note
> It applies to cross-die scenarios: cross-die within the same device, cross-device within the same node, and cross-node are all supported. The signal is delivered to the peer through the channel.

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {
CcuResult notify_record(ChannelHandle channel, uint32_t remote_notify_idx, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| channel | Input | Communication channel handle, of type [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md). |
| remote_notify_idx | Input | Peer notify slot index, ranging from 0 to 7. |
| mask | Input | 16-bit event mask that specifies the peer notify flag to set. The default value is `1`, that is, bit0. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation phase. |

## Usage Constraints

- It cannot be called inside the loop body of a hardware loop. The loop body is replicated into multiple copies for parallel execution, and each copy executes `notify_record` once, making the set semantics ambiguous.
- All channels within the same kernel must belong to the same die. Channels of different dies cannot be mixed into the same kernel. This constraint is uniformly checked by `<<<>>>`, and `CCU_E_PARA` is returned when it is violated.
- The `remote_notify_idx` of `notify_record` and the `local_notify_idx` of `notify_wait` have the same value.
- `notify_record` and `notify_wait` use the same `mask`.
- A single [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md) provides at most 8 notify slots, so `remote_notify_idx` ranges from 0 to 7.

## Call Example

```cpp
// For simplicity, return value reception and error handling are omitted.
using namespace AscendC::ccu;

// User-defined kernel argument struct, passed in through the kernelArg of <<<>>> after the host side establishes the link.
struct my_kernel_arg {
    ChannelHandle channel;
};

// Scenario: die A notifies die B that data can be read after completing the write operation.
// Inside the CCU kernel function body of die A.
CcuResult sender_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*, so first cast it to the user argument struct.
    // ... Perform the write operation to write data to the peer memory ...

    // Notify slot 3 of die B that bit0 is complete.
    notify_record(params->channel, /*remote_notify_idx=*/3, 0x1);
    return CCU_SUCCESS;
}

// Inside the CCU kernel function body of die B, corresponding to the kernel of die A.
CcuResult receiver_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // Cast ccu_kernel_arg, which is void*, to the user input parameter struct.
    // Wait for the signal from die A, bit0 of the local notify slot 3.
    notify_wait(params->channel, /*local_notify_idx=*/3, 0x1);

    // After this, the data written by die A can be safely read.
    return CCU_SUCCESS;
}
```
