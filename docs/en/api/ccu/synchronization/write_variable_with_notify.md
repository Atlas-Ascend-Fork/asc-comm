# write_variable_with_notify

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:45:36.899Z pushedAt=2026-11-09T06:49:22.791Z nl=2 -->

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

Header file: `#include "ccu/hcomm/ccu_primitives.hpp"`.

Within a CCU kernel, this API combines "writing the variable value to the remote end" and "triggering the remote notify signal" into a single atomic operation. The hardware guarantees the completion order of "write the value first, then send the notification". The consumer waits for the signal through [notify_wait](notify_wait.md) and then reads the newly written value through `AscendC::ccu::get_res_by_channel<variable>(channel, remote_var_idx)`.

> [!CAUTION] Caution
> Do not split this API into two independent calls of "writing a variable" and "notify_record". The hardware layer does not guarantee the arrival order of two independent operations. When the consumer receives the notify signal and then reads the remote variable, there is a risk of reading a stale value. Only this API can guarantee the order at the hardware layer.

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {
CcuResult write_variable_with_notify(ChannelHandle channel, variable var,
                                    uint32_t remote_var_idx, uint32_t remote_notify_idx,
                                    uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| channel | Input | Communication channel handle, which is the channel resource obtained through the link establishment process. The type is [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md). |
| var | Input | Local `variable` object whose current value is written to the remote end. |
| remote_var_idx | Input | Remote variable slot index. The consumer references the same slot through `get_res_by_channel<variable>(channel, remote_var_idx)`. |
| remote_notify_idx | Input | Remote notify slot index. It must use the same value as the `local_notify_idx` of the consumer's `notify_wait`. |
| mask | Input | 16-bit event mask that specifies the remote notify flag bits to set. The default value is `1`, that is, bit0. It must be consistent with the `mask` of the consumer's `notify_wait`. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_NOT_FOUND` | The variable handle corresponding to `var` is not created in the current kernel. |
| `CCU_E_PTR` | The API is called outside the compilation phase. |

## Usage Constraints

- It cannot be called inside the loop body of a hardware loop.
- All channels in the same kernel must belong to the same die. Channels of different dies cannot be mixed into the same kernel. This constraint is uniformly checked by `<<<>>>`, and `CCU_E_PARA` is returned when it is violated.
- `remote_var_idx` and `remote_notify_idx` are both channel-level slot numbers, passed in by the kernels on both ends respectively. The framework neither assigns the numbers nor checks whether the two ends are consistent. You must ensure that both ends use the same numbers; otherwise, the consumer will be blocked permanently.

## Call Example

```cpp
// For simplicity, return value reception and error handling are omitted.
using namespace AscendC::ccu;

// User-defined kernel argument struct, passed through the kernelArg of <<<>>> after link establishment on the host side.
struct my_kernel_arg {
    ChannelHandle channel;
};

// Scenario: the producer sends the progress value to the consumer, and the consumer reads it after receiving the notification.
// Inside the producer CCU kernel function body (die A).
CcuResult producer_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*, so first cast it to the user argument struct.
    variable progress;
    progress = 100;  // Assign the immediate value 100.

    // Atomic operation: write to the peer variable slot 2 and notify bit0 of the peer notify slot 3.
    write_variable_with_notify(params->channel, progress,
                               /*remote_var_idx=*/2, /*remote_notify_idx=*/3, 0x1);
    return CCU_SUCCESS;
}

// Inside the consumer CCU kernel function body (die B).
CcuResult consumer_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*, so first cast it to the user argument struct.
    // Wait for the signal from die A.
    notify_wait(params->channel, /*local_notify_idx=*/3, 0x1);

    // Safely read the value written by die A (ensuring the write is complete).
    variable received = get_res_by_channel<variable>(params->channel, /*var_idx=*/2);
    return CCU_SUCCESS;
}
```
