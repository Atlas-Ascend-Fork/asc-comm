# loop_group

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件为：`#include "ccu/hcomm/ccu_primitives.hpp"`

CCU kernel内的硬件LoopGroup类，将多个`ccu::loop`对象组织为一组，共享同一份LoopEngine资源池，避免多个独立`loop`各自独占LoopEngine导致资源耗尽。

构造`ccu::loop_group`时，自动将传入的`loops`列表中的每个`loop`加入该Group。

## 类声明

```cpp
namespace AscendC {
namespace ccu {

class loop_group {
public:
    // config-based：Group参数在注册阶段确定
    loop_group(const ccu_loop_group_config &loopGroupCfg, uint32_t max_loop_num,
              const std::vector<loop> &loops);

    // var-based：Group参数在执行阶段由variable值决定
    loop_group(variable &parallelCfg, variable &offsetCfg, uint32_t max_loop_num,
              const std::vector<loop> &loops);
};

} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| loopGroupCfg | 输入 | config-based的Group配置，类型为[`ccu_loop_group_config`](#loop-group-cfg-config-based)。 |
| parallelCfg | 输入 | var-based的并行配置`variable`，执行阶段决定并行参数，位段定义见[var-based：`parallel_cfg`与`offset_cfg`](#var-based-parallel-offset)。 |
| offsetCfg | 输入 | var-based的偏移配置`variable`，执行阶段决定各实例使用的资源偏移，位段定义见[var-based：`parallel_cfg`与`offset_cfg`](#var-based-parallel-offset)。 |
| max_loop_num | 输入 | 本Group最多容纳的Loop数量，框架据此预留LoopEngine资源池容量。 |
| loops | 输入 | 要加入本Group的`ccu::loop`对象列表。列表中每个Loop会在`loop_group`构造函数内自动被注册到Group。 |

### config-based：`ccu_loop_group_config`<a id="loop-group-cfg-config-based"></a>

config-based构造的`loop_group_cfg`类型，字段如下：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `clone_num` | `uint32_t` | 复制份数，指定Group内并发执行的实例数。 |
| `clone_loop_offset` | `uint32_t` | 克隆实例间Loop的偏移量。 |
| `addr_offset` | `uint32_t` | 克隆实例间`address`的偏移字节数。 |
| `ccu_buffer_offset` | `uint32_t` | 克隆实例间`ccu_buffer`切片的偏移片数。 |
| `event_offset` | `uint32_t` | 克隆实例间`event`槽位的偏移数量。 |

### var-based：`parallel_cfg`与`offset_cfg`<a id="var-based-parallel-offset"></a>

`parallel_cfg`有64个比特位，按位段定义：

| 位段 | 说明 |
| --- | --- |
| [47:41] | Group内Loop指令个数。 |
| [54:48] | 开始复制的Loop编号。 |
| [61:55] | 每个被复制Loop的复制次数。 |

举例：[47:41]=4、[54:48]=1、[61:55]=3表示共4个Loop指令，从编号1的Loop开始，loop1至loop3各复制3份、loop0不复制，展开后总Loop个数为4 + (4-1) × 3 = 13。

`offset_cfg`有64个比特位，按位段定义：

| 位段 | 说明 |
| --- | --- |
| [9:0] | event资源偏移量。 |
| [20:10] | ccu_buffer资源偏移量。 |
| [52:21] | 数据传输类指令的address递增步长。 |

## 返回值说明

无返回值。构造失败时抛出异常（携带[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)错误码），被`<<<>>>`统一捕获后以错误码返回。常见原因：

| 原因 | 错误码 |
| --- | --- |
| `max_loop_num`为0，或var-based构造传入的`variable`无效 | `CCU_E_PARA` |
| 实际加入的loop数超过`max_loop_num`（LoopEngine池容量不足） | `CCU_E_PARA` |
| 物理资源不足 | `CCU_E_UNAVAIL`等 |

## 约束说明

- `ccu::loop_group`构造时会立即遍历`loops`列表，将每个Loop注册到Group，注册后不可再修改Group成员。
- `loops`列表中的`ccu::loop`对象必须在`ccu::loop_group`构造前完成构造，即循环体已记录。
- 同一个`ccu::loop`对象不应被加入多个`ccu::loop_group`。
- Group内各Loop的循环体约束与独立`ccu::loop`相同（参见[loop](loop.md)的约束说明）。
- `max_loop_num`必须大于0，为0时构造直接失败（`CCU_E_PARA`）。
- `max_loop_num`应 ≥ `loops`列表实际大小（即真正会加入Group的Loop数，含展开复用）；偏小会导致后续加入Loop时因LoopEngine池容量不足而失败（`CCU_E_PARA`）。

## 调用示例

- config-based：
    ```cpp
    using namespace AscendC::ccu;

    // 场景：两个loop共享LoopEngine资源池
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

        // 将l1, l2组织成loop_group，共享LoopEngine资源池
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

- var-based：
    ```cpp
    // 用户自定义的kernel入参结构体，Host侧填充后通过<<<>>>的kernelArg传入
    struct my_kernel_arg {
        uint64_t numA;
        uint64_t numB;
    };

    CcuResult my_kernel(ccu_kernel_arg arg)
    {
        using namespace ccu;
        auto* args = static_cast<my_kernel_arg*>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体

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
