# loop

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:43:44.944Z pushedAt=2026-11-09T06:47:55.018Z nl=2 -->

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

`ccu::loop` is a hardware loop class within the CCU kernel, used to describe a piece of copy logic that needs to be executed repeatedly.

Only one copy of the loop body instructions is recorded. During the delivery phase, the hardware **LoopEngine** automatically iterates the execution, updating the address, buffer, and event according to the configured offset in each round, without the need to manually write address increments or counter updates inside the loop body. Therefore, it is suitable for scenarios with a large number of iterations of the same structure and only local copy operations inside the loop body, with low instruction overhead.

> [!NOTE] Note
> Creating a `ccu::loop` object only records the loop body as instructions; it does not iterate during the delivery phase. You must add the `loop` to [`ccu::loop_group`](loop_group.md) so that the hardware iterates according to the configuration during the delivery phase.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {

class loop {
public:
    // config-based: iteration parameters are determined at compile time.
    loop(const ccu_loop_config &loop_cfg, const func &func);

    // var-based: iteration parameters are determined by the variable value during the delivery phase.
    loop(variable &loop_cfg, const func &func);
};

} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| loop_cfg | Input | Loop configuration.<br>&bull;config-based: Type is [`ccu_loop_config`](#loop-cfg-config-based).<br>&bull;var-based: Type is [ccu::variable](#loop-cfg-var-based). |
| func | Input | Loop body of the loop, of type [func](func.md), consisting of a C++ lambda with no input parameters. The lambda executes once immediately upon construction, and the API calls within it are recorded as loop body instructions. |

### config-based: `ccu_loop_config`<a id="loop-cfg-config-based"></a>

Fields of the `loop_cfg` type constructed in config-based mode:

| Field | Type | Description |
| --- | --- | --- |
| `addr_offset` | `uint64_t` | Number of bytes by which `address` is automatically offset in each iteration. |
| `iter_num` | `uint64_t` | Total number of iterations. |

### var-based: `ccu::variable`<a id="loop-cfg-var-based"></a>

The `loop_cfg` constructed in var-based mode has 64 bits, defined by bit fields as follows:

| Bit Field | Description |
| --- | --- |
| [12:0] | Number of loop iterations. |
| [44:13] | Address offset of data transfer instructions. |
| [52:45] | LoopEngine ID. You must fill in `0`; the system automatically assigns and fills it in during the delivery phase. |

## Return Value

None. If construction fails, an exception is thrown and uniformly caught by `<<<>>>`.

## Usage Constraints

- The lambda of the loop body must have no input parameters. If it has input parameters, `<<<>>>` throws the `CCU_E_PARA` exception.
- After construction is complete, no more content can be appended to this loop.
- API restrictions within the loop body:
  - [event_record](../synchronization/event_record.md) cannot be called. If called, it returns `CCU_E_NOT_SUPPORT`.
  - Software branch macros such as [CCU_IF](CCU_IF.md) cannot be used. The code does not report an error, but the hardware exhibits undefined behavior.
  - [notify_record](../synchronization/notify_record.md) and [write_variable_with_notify](../synchronization/write_variable_with_notify.md) do not report an error, but when the loop body is replicated into multiple copies for parallel execution, the signal is sent repeatedly. Avoid using them.
- `ccu::loop` must be added to a `ccu::loop_group` to issue hardware loop instructions. Do not use it alone. For details, see [Function Description](#function-description).
- A `ccu::loop` object cannot be added to a `ccu::loop_group` repeatedly.

## Call Example

```cpp
using namespace AscendC::ccu;

// Scenario: Move four contiguous 4 KB data blocks from on-chip memory to the CCU Buffer in sequence.
// The hardware loop updates the address by addr_offset each round, so no manual address increment is required.
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable r1, numA, numB;
    numA = 10;
    numB = 20;

    // Define the loop body: a lambda with no input parameters.
    func body([&] {
        r1 = numA + numB;   // Perform addition each round.
    });

    // config-based: iterate four times with a fixed address offset of 4096 bytes per round.
    ccu_loop_config cfg;
    cfg.addr_offset = 4096;
    cfg.iter_num = 4;
    loop l(cfg, body);   // The loop body is recorded immediately at construction, but no hardware loop instruction is issued yet.

    // Key point: The loop instruction is actually issued only after the loop is added to a LoopGroup.
    ccu_loop_group_config grp_cfg{};   // For a single-loop scenario, set all fields to 0.
    loop_group g(grp_cfg, /*max_loop_num=*/1, {l});

    return CCU_SUCCESS;
}
```
