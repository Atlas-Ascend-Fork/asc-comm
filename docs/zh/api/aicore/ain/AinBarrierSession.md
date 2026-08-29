# AinBarrierSession

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持URMA协议
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90：不支持
<!-- end id7 -->

## 功能说明

头文件为：

```cpp
#include "ain/ain.h"
```

`AscendC::AinBarrierSession`是AICore侧基于AIN signal实现的team内同步会话。构造时绑定`Ain`实例、通信team和barrier资源索引；调用`Sync`时对team内其他rank发送signal，并等待其他rank对应signal达到阈值。

## 函数原型

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class AinBarrierSession;
```

构造函数：

```cpp
__aicore__ inline AinBarrierSession(
    AscendC::Ain<CommEngineMask>* ain,
    AscendC::HcommTeamHandle team,
    uint32_t index);
```

## 参数说明

**表1**  模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `CommEngineMask` | 通信引擎选择掩码，默认值为`AIN_MASK_DEFAULT`。当前为预留参数。 |

**表2**  接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `ain` | 输入 | `Ain`实例指针。 |
| `team` | 输入 | 通信team句柄。 |
| `index` | 输入 | barrier资源索引，不同索引用于隔离不同barrier会话。 |

## 常用接口

| 接口 | 描述 |
| --- | --- |
| [Sync](./Sync.md) | 执行team内barrier同步，支持无超时和带超时两种重载。 |

## 约束说明

- 调用前Host侧需完成team创建，并准备barrier使用的同步内存。
- `ain`不能为nullptr；`team`需要是有效的device侧句柄。
- `index`需要对应可用的barrier资源，不同并发barrier应使用不同`index`隔离。
- `Sync`内部会通过底层Hcomm原子加更新远端signal，并轮询本端signal等待其他rank到达。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。

## 调用示例

AinBarrierSession接口的调用示例如下。

```cpp
AscendC::Ain<> ain;
AscendC::AinBarrierSession<> barrierSession(&ain, team, 0);
barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX);    // 执行team内barrier同步
```
