# get_res_by_channel

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-05T01:34:58.024Z pushedAt=2026-11-09T06:49:22.778Z nl=2 -->

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

Obtains the handle to a channel-shared `variable` slot within a CCU kernel.

This slot is a shared `variable` that has already been reserved on the local end when the channel establishes a link and is paired one-to-one with the remote end by slot number. This API does not consume additional general-purpose registers; it only wraps the existing slot into an operable `variable` object.

Typical scenario: the remote rank writes a value into shared `variable` slot N of the local channel through [write_variable_with_notify](../synchronization/write_variable_with_notify.md), and the local kernel uses `get_res_by_channel<variable>(ch, N)` to obtain the handle to the same slot and read the value sent by the remote end.

## Prototype

```cpp
namespace AscendC {
namespace ccu {
template <typename T>
T get_res_by_channel(ChannelHandle channel, uint32_t index);

// Currently only the variable specialization is supported:
template <>
variable get_res_by_channel<variable>(ChannelHandle channel, uint32_t var_index);
} // namespace ccu
} // namespace AscendC
```

## Parameters

The template parameter `T` is the type of the resource to obtain. Currently only `variable` is supported.

| Parameter | Input/Output | Description |
| --- | --- | --- |
| channel | Input | Cross-rank channel handle, of type [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md), which must have completed link establishment. |
| var_index | Input | Variable index within the channel (starting from 0), corresponding to one of the variable slots pre-allocated when the channel establishes a link. Each channel currently reserves 4 variable slots (`var_index` ranges from `[0, 3]`); values outside this range return `CCU_E_PARA`. |

## Return Value

Returns a `variable` object that points to the `var_index`-th shared `variable` slot of the channel. Its release right belongs to the channel and is not released when the returned `variable` is destroyed.

If the call fails, an exception is thrown (carrying an error code). Common error codes:

| Scenario | Error Code |
| --- | --- |
| `channel == nullptr`, invalid channel type, or the API is called outside the compilation phase | `CCU_E_PTR` |
| `var_index` out of range (exceeding the size of the pre-allocated variable pool of the channel) | `CCU_E_PARA` |

## Usage Constraints

- It can only be called at compile time.
- The allocation behavior is completely different from that of `variable v;` (ordinary construction), and the two are not interchangeable.
- The lifetime of the returned `variable` is managed by the channel and remains valid until the channel is destroyed.
- Currently, only `T = variable` is supported. Calling with other types results in a compile-time failure.

## Call Example

```cpp
using namespace AscendC::ccu;

// User-defined kernel argument structure, filled on the host side and passed in through kernelArg.
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// End-to-end scenario: the peer writes a value to shared variable slot 0 of the local channel through write_variable_with_notify.
// The local kernel reads the value through get_res_by_channel.

CcuResult my_kernel(ccu_kernel_arg arg) {  // arg is void*, pointing to my_kernel_arg passed from the host.
    auto* params = static_cast<my_kernel_arg*>(arg);
    ChannelHandle ch = params->channel_handle;

    // Obtain the variable[0] preallocated by the channel without allocating a new variable.
    variable sync_var = get_res_by_channel<variable>(ch, 0);

    // Wait for the peer to write.
    notify_wait(ch, /*local_notify_idx=*/0);

    // At this point, sync_var holds the value written by the peer and can be read for subsequent computation.
    variable result;
    result = sync_var;

    return CCU_SUCCESS;
}
```
