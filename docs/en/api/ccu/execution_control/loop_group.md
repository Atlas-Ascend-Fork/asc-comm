# loop_group

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:42:27.341Z pushedAt=2026-11-09T06:47:55.017Z nl=2 -->

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

A hardware **LoopGroup** class in the CCU kernel that organizes multiple `ccu::loop` objects into a group to share the same **LoopEngine** resource pool, preventing resource exhaustion caused by multiple independent `loop` objects each exclusively occupying the **LoopEngine**.

When constructing `ccu::loop_group`, each `loop` in the passed `loops` list is automatically added to the group.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {

class loop_group {
public:
    // config-based: Group parameters are determined at compile time.
    loop_group(const ccu_loop_group_config &loopGroupCfg, uint32_t max_loop_num,
              const std::vector<loop> &loops);

    // var-based: Group parameters are determined by variable values at the dispatch stage.
    loop_group(variable &parallelCfg, variable &offsetCfg, uint32_t max_loop_num,
              const std::vector<loop> &loops);
};

} // namespace ccu
} // namespace AscendC
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| loopGroupCfg | Input | config-based group configuration, of type [`ccu_loop_group_config`](#loop-group-cfg-config-based). |
| parallelCfg | Input | var-based parallel configuration `variable`. The parallel parameters are determined at the dispatch stage. For the bit field definition, see [var-based: `parallel_cfg` and `offset_cfg`](#var-based-parallel-offset). |
| offsetCfg | Input | var-based offset configuration `variable`. The resource offset used by each instance is determined at the dispatch stage. For the bit field definition, see [var-based: `parallel_cfg` and `offset_cfg`](#var-based-parallel-offset). |
| max_loop_num | Input | Maximum number of loops that this group can hold. The framework reserves the **LoopEngine** resource pool capacity accordingly. |
| loops | Input | List of `ccu::loop` objects to be added to this group. Each loop in the list is automatically added to the group in the `loop_group` constructor. |

### config-based: `ccu_loop_group_config`<a id="loop-group-cfg-config-based"></a>

The `loop_group_cfg` type constructed in a config-based manner has the following fields:

| Field | Type | Description |
| --- | --- | --- |
| `clone_num` | `uint32_t` | Number of copies, specifying the number of instances executed concurrently in the group. |
| `clone_loop_offset` | `uint32_t` | Loop offset between clone instances. |
| `addr_offset` | `uint32_t` | Byte offset of `address` between clone instances. |
| `ccu_buffer_offset` | `uint32_t` | Slice offset of `ccu_buffer` between clone instances. |
| `event_offset` | `uint32_t` | Offset number of `event` slots between clone instances. |

### var-based: `parallel_cfg` and `offset_cfg`<a id="var-based-parallel-offset"></a>

`parallel_cfg` has 64 bits, defined by bit fields:

| Bit Field | Description |
| --- | --- |
| [47:41] | Number of loop instructions in the group. |
| [54:48] | Loop number at which replication starts. |
| [61:55] | Replication count of each replicated loop. |

Example: [47:41]=4, [54:48]=1, and [61:55]=3 indicate a total of 4 loop instructions. Starting from loop 1, loop1 through loop3 are each replicated 3 times, while loop0 is not replicated. After expansion, the total number of loops is 4 + (4-1) × 3 = 13.

`offset_cfg` has 64 bits, defined by bit fields:

| Bit Field | Description |
| --- | --- |
| [9:0] | Event resource offset. |
| [20:10] | ccu_buffer resource offset. |
| [52:21] | Address increment step for data transfer instructions. |

## Return Value

None. If construction fails, an exception is thrown (carrying the [`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/CcuResult.md) error code), which is uniformly caught by `<<<>>>` and returned as an error code. Common reasons:

| Reason | Error Code |
| --- | --- |
| `max_loop_num` is 0, or the `variable` passed in var-based construction is invalid. | `CCU_E_PARA` |
| The number of loops actually added exceeds `max_loop_num` (insufficient LoopEngine pool capacity). | `CCU_E_PARA` |
| Insufficient physical resources. | `CCU_E_UNAVAIL`, etc. |

## Usage Constraints

- When `ccu::loop_group` is constructed, it immediately traverses the `loops` list and adds each loop to the group. After the loops are added, the group members cannot be modified.
- The `ccu::loop` objects in the `loops` list must have been constructed before the `ccu::loop_group` is constructed, that is, the loop body has been recorded.
- The same `ccu::loop` object should not be added to multiple `ccu::loop_group` objects.
- The loop body constraints of each loop in a group are the same as those of an independent `ccu::loop` (see the constraints in [loop](loop.md)).
- `max_loop_num` must be greater than 0. If it is 0, construction fails directly (`CCU_E_PARA`).
- `max_loop_num` should be greater than or equal to the actual size of the `loops` list (that is, the number of loops that will actually be added to the group, including those reused by unrolling). If it is too small, adding loops later will fail due to insufficient LoopEngine pool capacity (`CCU_E_PARA`).

## Call Example

- config-based:

    ```cpp
    using namespace AscendC::ccu;

    // Scenario: two loops share the LoopEngine resource pool.
    CcuResult my_kernel(ccu_kernel_arg arg) {
        variable r1, r2, numA, numB;
        numA = 10; numB = 20;

        func body1([&] { r1 = numA + numB; });
        func body2([&] { r2 = numA + numA; });

        ccu_loop_config cfg1;
        cfg1.addr_offset = 0;
        cfg1.iter_num = 2;
        loop l1(cfg1, body1);

        ccu_loop_config cfg2;
        cfg2.addr_offset = 0;
        cfg2.iter_num = 3;
        loop l2(cfg2, body2);

        // Organize l1 and l2 into a loop_group to share the LoopEngine resource pool.
        ccu_loop_group_config grp_cfg;
        grp_cfg.clone_num = 0;
        grp_cfg.clone_loop_offset = 0;
        grp_cfg.addr_offset = 0;
        grp_cfg.ccu_buffer_offset = 0;
        grp_cfg.event_offset = 0;
        loop_group g(grp_cfg, /*max_loop_num=*/2, {l1, l2});

        return CCU_SUCCESS;
    }
    ```

- var-based:

    ```cpp
    // User-defined kernel argument struct, filled on the host side and passed through the kernelArg of <<<>>>.
    struct my_kernel_arg {
        uint64_t numA;
        uint64_t numB;
    };

    CcuResult my_kernel(ccu_kernel_arg arg)
    {
        using namespace ccu;
        auto* args = static_cast<my_kernel_arg*>(arg);  // ccu_kernel_arg is void*. Cast it to the user argument struct first.

        variable numA{}, numB{}, r1{}, r2{};
        numA = args->numA;
        numB = args->numB;

        // ========== loop_group variable-based ==========
        variable var_loop_param1{}, var_loop_param2{}, var_parallel{}, var_offset{};

        // loopParam: ctxId[52:45] | addrOffset[44:13] | iter_num[12:0]
        var_loop_param1 = 0x0000000002000003ULL; // addrOffset=4096, iter_num=3
        var_loop_param2 = 0x0000000002000004ULL; // addrOffset=4096, iter_num=4

        // repeatNum=2, repeatLoopIndex=1, totalLoopNum=2
        var_parallel = 0x0101040000000000ULL;

        // addrOffset=4096, ccuBufferOffset=1, eventOffset=1
        var_offset = 0x0000000200000401ULL;

        func body1([&]() {
            r1 = numA + numB;
        });
        func body2([&]() {
            r2 = numA + numB;
        });

        loop loop1(var_loop_param1, body1);
        loop loop2(var_loop_param2, body2);

        loop_group group(var_parallel, var_offset, /*max_loop_num=*/4, {loop1, loop2});

        return CCU_SUCCESS;
    }
    ```
