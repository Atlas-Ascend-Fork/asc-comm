# WriteVariableWithNotify

## 产品支持情况

- Ascend 950PR/Ascend 950DT：支持
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
- Atlas 200I/500 A2 推理产品：不支持
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
- Atlas 训练系列产品：不支持
- Kirin X90：不支持
- Kirin 9030：不支持

## 功能说明

头文件为：`#include "include/ccu/ccu_primitives.hpp"`

抽象语义为：

$$remoteVar[remoteVarIdx] \leftarrow var,\quad notify[remoteNotifyIdx] \leftarrow notify[remoteNotifyIdx] \lor mask$$

在CCU kernel内将"写远端Variable值"与"触发远端Notify信号"合并为单条原子操作，硬件保证"先写值后发Notify"的完成顺序。

> [!CAUTION]注意
> 不可将本接口拆分为独立的"写Variable"与"NotifyRecord"两步调用。硬件层不保证两条独立操作的到达顺序，消费侧收到Notify信号后读取远端Variable时，存在读到旧值的风险。仅本接口能从硬件层保证顺序。

消费侧通过[NotifyWait](NotifyWait.md)等待信号，再通过`AscendC::ccu::GetResByChannel<Variable>(channel, remoteVarIdx)`读取写入的新值。

## 函数原型

```cpp
namespace AscendC {
namespace ccu {
CcuResult WriteVariableWithNotify(ChannelHandle channel, Variable var,
                                  uint32_t remoteVarIdx, uint32_t remoteNotifyIdx,
                                  uint16_t mask = 1);
} // namespace ccu
} // namespace AscendC
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| channel | 输入 | 通信通道句柄，为通过建链流程获取的通道资源。`ChannelHandle`类型的定义可参见`ChannelHandle`。channel绑定的die必须与本kernel当前die一致。 |
| var | 输入 | 本端Variable对象，其当前值将被写入远端channel-bound Variable槽位。 |
| remoteVarIdx | 输入 | 远端Variable槽位索引（0-based），为channel两端协商约定的编号。消费侧通过`GetResByChannel<Variable>(channel, remoteVarIdx)`引用同一槽位。 |
| remoteNotifyIdx | 输入 | 远端notify槽位索引（0-based），为channel两端协商约定的编号。必须与消费侧`NotifyWait`的`localNotifyIdx`数值完全一致。 |
| mask | 输入 | 16位事件掩码，指定要置位的bit。默认值为`1`。必须与消费侧`NotifyWait`的`mask`一致。 |

## 返回值说明

`CcuResult`：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_NOT_FOUND` | `var`对应的Variable句柄未在当前kernel注册（句柄无效）。 |
| `CCU_E_PTR` | 当前不存在处于注册中的kernel（接口在kernel注册阶段之外被调用）。 |

> [!NOTE]说明
> 本接口在调用处不校验channel/`remoteVarIdx`/`remoteNotifyIdx`；channel绑定die的一致性由`HcommCcuKernelRegister`统一校验（依据本kernel期间用到的全部channel），不一致时返回 `CCU_E_PARA`。本接口不拦截硬件Loop body内调用，请勿在Loop body内使用。

## 约束说明

- 本kernel内所有channel必须属于同一die；该一致性由`HcommCcuKernelRegister`统一校验，不一致返回`CCU_E_PARA`。
- `remoteVarIdx`与`remoteNotifyIdx`均为channel级别的槽位编号，由通信双方在协议层约定，框架不自动分配。
- 禁止将本接口拆分为独立的Variable写操作与`NotifyRecord`调用，否则硬件无法保证值写入与Notify到达的顺序，消费侧可能读到旧值。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 场景：生产者将进度值发给消费者，消费者收到通知后读取
// 生产者CCU kernel函数体内（die A）
CcuResult ProducerKernel(CcuKernelArg arg) {
    auto *params = static_cast<MyKernelArg *>(arg);  // CcuKernelArg 为void*，先转型为用户入参结构体
    Variable progress;
    progress = 100;  // 赋立即数100

    // 原子操作：写远端变量槽位2 + 通知远端槽位3的bit0
    WriteVariableWithNotify(params->channel, progress,
                            /*remoteVarIdx=*/2, /*remoteNotifyIdx=*/3, 0x1);
    return CCU_SUCCESS;
}

// 消费者CCU kernel函数体内（die B）
CcuResult ConsumerKernel(CcuKernelArg arg) {
    auto *params = static_cast<MyKernelArg *>(arg);  // CcuKernelArg 为void*，先转型为用户入参结构体
    // 等待来自die A 的信号
    NotifyWait(params->channel, /*localNotifyIdx=*/3, 0x1);

    // 安全读取die A 写入的值（保证已写入完成）
    Variable received = GetResByChannel<Variable>(params->channel, /*varIdx=*/2);
    return CCU_SUCCESS;
}
```
