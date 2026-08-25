# Hcomm

## 功能说明

头文件为：

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm`是AICore侧点对点通信接口模板，支持通过通信通道提交读、写、写通知和原子操作任务，并通过`Commit`和`Drain`控制任务提交与完成等待。本文档说明RoCE和UBC_CTP/URMA路径的协议能力和使用约束。

## 模板参数

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| 参数 | 说明 |
| --- | --- |
| `commProtocol` | 通信协议类型，支持`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UBC_CTP`，默认值为`COMM_PROTOCOL_UBC_CTP`。 |

## 协议能力

| 接口 | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | 支持，需要提供UB临时工作区。 | 支持，需要提供URMA临时工作区。 |
| `ReadNbi` | 支持。 | 支持。 |
| `WriteNbi` | 支持。 | 支持。 |
| `WriteWithNotifyNbi` | 不支持，调用会返回失败。 | 支持。 |
| `AtomicFAA` | 不支持。 | 支持。 |
| `AtomicCAS` | 不支持。 | 支持。 |
| `Commit` | 支持。 | 支持。 |
| `Drain` | 支持。 | 支持。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [Init](./Init.md) | 初始化Hcomm临时工作区。 |
| [ReadNbi](./ReadNbi.md) | 通过指定通道提交读任务。 |
| [WriteNbi](./WriteNbi.md) | 通过指定通道提交写任务。 |
| [WriteWithNotifyNbi](./WriteWithNotifyNbi.md) | 提交写任务并写通知值。 |
| [AtomicFAA](./AtomicFAA.md) | 提交Fetch-and-add原子操作任务。 |
| [AtomicCAS](./AtomicCAS.md) | 提交Compare-and-swap原子操作任务。 |
| [Commit](./Commit.md) | 显式提交通道上的待执行任务。 |
| [Drain](./Drain.md) | 等待通道上的通信任务完成。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 执行成功。 |
| `-1` | 执行失败。 |

## 使用约束

- 调用通信接口前，通信通道需要由调用方完成初始化。
- `COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UBC_CTP`路径均需要通过`Init`提供临时工作区，不同协议使用的临时工作区布局不同。
- `WriteWithNotifyNbi`仅支持`COMM_PROTOCOL_UBC_CTP`路径，`COMM_PROTOCOL_ROCE`路径会返回失败。
- `AtomicFAA`和`AtomicCAS`仅支持`COMM_PROTOCOL_UBC_CTP`路径，数据类型仅支持`int32_t`、`uint32_t`、`int64_t`、`uint64_t`。
- 传入的`ChannelHandle`需要指向与协议匹配的通道实体。

## 相关样例

[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README.md)演示两卡场景下，AIV Kernel通过`COMM_PROTOCOL_UBC_CTP`路径调用`WriteNbi`和`ReadNbi`。该样例不覆盖RoCE路径。
