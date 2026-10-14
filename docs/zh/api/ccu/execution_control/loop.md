# loop

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

`ccu::loop`是CCU kernel内的硬件循环类，用于描述一段需要重复执行的搬运逻辑。

循环体的指令只记录一份。执行阶段由硬件LoopEngine自动迭代执行，每轮按配置的偏移更新地址、buffer和event，无须在循环体内手写地址自增或计数器更新。因此适合大量同结构迭代、循环体内只有本地搬运类操作的场景，指令开销小。

> [!NOTE]说明
> 创建`ccu::loop`对象只是把循环体记录为指令，执行阶段不会迭代；必须将`loop`加入[`ccu::loop_group`](loop_group.md)，执行阶段硬件才会按配置迭代。

## 类声明

```cpp
namespace AscendC {
namespace ccu {

class loop {
public:
    // config-based：迭代参数在注册阶段确定
    loop(const ccu_loop_config &loop_cfg, const func &func);

    // var-based：迭代参数在执行阶段由variable值决定
    loop(variable &loop_cfg, const func &func);
};

} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| loop_cfg | 输入 | Loop配置。<br>&bull;config-based：类型为[`ccu_loop_config`](#loop-cfg-config-based)。<br>&bull;var-based：类型为[ccu::variable](#loop-cfg-var-based)。 |
| func | 输入 | Loop的循环体，类型为[func](func.md)，由一段无入参的C++ lambda代码构成。构造时lambda立即执行一次，其中的接口调用被记录为循环体指令。 |

### config-based：`ccu_loop_config`<a id="loop-cfg-config-based"></a>

config-based构造的`loop_cfg`类型，字段如下：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `addr_offset` | `uint64_t` | 每轮迭代，`address`自动偏移的字节数。 |
| `iter_num` | `uint64_t` | 迭代总次数。 |

### var-based：`ccu::variable`<a id="loop-cfg-var-based"></a>

var-based构造的`loop_cfg`有64个比特位，按位段定义：

| 位段 | 说明 |
| --- | --- |
| [12:0] | Loop的循环次数。 |
| [44:13] | 数据传输类指令的地址偏移量。 |
| [52:45] | LoopEngine编号，用户固定填`0`，由系统在执行阶段自动分配填入。 |

## 返回值说明

无返回值。构造失败时抛出异常，被`<<<>>>`统一捕获。

## 约束说明

- 循环体的lambda必须无入参，有入参时`<<<>>>`抛出异常`CCU_E_PARA`。
- 构造完成后不可再向该Loop追加内容。
- 循环体内的接口限制：
  - [event_record](../synchronization/event_record.md)不可调用，调用时返回`CCU_E_NOT_SUPPORT`。
  - [CCU_IF](CCU_IF.md)等软件分支宏不可使用，代码不会报错，但硬件会发生未定义行为。
  - [notify_record](../synchronization/notify_record.md)和[write_variable_with_notify](../synchronization/write_variable_with_notify.md)不会报错，但循环体被复制为多份并行执行时信号会被重复发送，应避免使用。
- `ccu::loop`必须加入`ccu::loop_group`才能下发硬件循环指令，不要单独使用，详见[功能说明](#功能说明)。
- `ccu::loop`对象不可在`ccu::loop_group`中被重复添加。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 场景：将4个连续4KB数据块依次从片上内存搬到CCU Buffer
// 硬件Loop每轮按addr_offset更新地址，无需手写地址自增
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable r1, numA, numB;
    numA = 10;
    numB = 20;

    // 定义循环体：无入参lambda
    func body([&] {
        r1 = numA + numB;   // 每轮执行加法
    });

    // config-based：固定迭代4次，地址每轮偏移4096字节
    ccu_loop_config cfg;
    cfg.addr_offset = 4096;
    cfg.iter_num = 4;
    loop l(cfg, body);   // 构造时循环体立即记录，但未下发硬件循环指令

    // 关键：必须加入LoopGroup才会真正下发循环指令
    ccu_loop_group_config grp_cfg{};   // 单Loop场景全字段置0即可
    loop_group g(grp_cfg, /*max_loop_num=*/1, {l});

    return CCU_SUCCESS;
}
```
