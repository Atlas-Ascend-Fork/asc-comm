# notify_wait

<!-- md-trans-meta sourceCommit=663c3729c34015dda67a1bfe5910893f18045deb translatedAt=2026-11-05T01:44:19.726Z pushedAt=2026-11-09T06:49:22.788Z nl=2 -->

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

Blocks and waits for the notify flags corresponding to `mask` in the local synchronization register until all of them are set to 1, then continues execution. These flags are set by [notify_record](notify_record.md) on the peer die through the channel.

> [!NOTE] Note
> It applies to cross-die scenarios: within the same device across dies, across devices on the same node, and across nodes. Signals are delivered to the peer through the channel.

## Function Prototype

```cpp
namespace AscendC {
namespace ccu {
CcuResult notify_wait(ChannelHandle channel, uint32_t local_notify_idx, uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| channel | Input | Communication channel handle, of type [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md). |
| local_notify_idx | Input | Local notify slot index, ranging from 0 to 7. |
| mask | Input | 16-bit event mask that specifies the local notify flags to wait for. The default value is `1`, that is, bit0. |

## Return Value

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md): The API returns `CCU_SUCCESS` on success, and other values on failure.

| Return Value | Description |
| --- | --- |
| `CCU_SUCCESS` | The operation is successful. |
| `CCU_E_PTR` | The API is called outside the compilation phase. |

## Usage Constraints

- `notify_wait` can be called inside the body of a hardware loop.
- All channels within the same kernel must belong to the same die. Channels of different dies cannot be mixed into the same kernel. This constraint is uniformly verified by `<<<>>>`, and `CCU_E_PARA` is returned when it is violated.
- The `remote_notify_idx` of `notify_record` and the `local_notify_idx` of `notify_wait` have the same value.
- `notify_record` and `notify_wait` use the same `mask`.
- A single [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md) provides up to 8 notify slots, so `local_notify_idx` ranges from 0 to 7.
- The local `notify_wait` must be paired with the [notify_record](notify_record.md) sent by the remote die through the corresponding channel, which sets the local notify flag. The remote side may send the notification before or after the local side starts waiting. Calling `notify_record` on the local side only notifies the remote side and cannot substitute for the remote side sending the notification that the local side is waiting for. If the remote side never sends a matching notification, this API blocks indefinitely, which may cause a hardware-level deadlock.

## Call Example

```cpp
// For simplicity, the example omits return value handling and error handling.
using namespace AscendC::ccu;

// User-defined kernel input parameter struct, passed through the kernelArg of <<<>>> after link establishment on the host side.
struct my_kernel_arg {
    ChannelHandle channel;
};

// Scenario: die B waits for die A to complete the write operation before reading data.
// Inside the CCU kernel function body of die B.
CcuResult receiver_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg is void*, so first cast it to the user input parameter struct.
    // Wait for the signal from die A, bit0 of local notify slot 3.
    // Corresponds to notify_record(channel, /*remote_notify_idx=*/3, 0x1) on the die A side.
    notify_wait(params->channel, /*local_notify_idx=*/3, 0x1);

    // After this, data written by die A can be safely read.
    remote_addr remote;
    local_addr dst;
    variable len;
    event evt;
    read(params->channel, dst, remote, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
