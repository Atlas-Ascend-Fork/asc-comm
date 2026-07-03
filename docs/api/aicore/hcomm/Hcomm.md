# Hcomm

## 功能说明

头文件为：

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm`是AICore侧点对点通信接口模板，支持通过通信通道提交读写任务，并通过`Commit`和`Drain`控制任务提交与完成等待。该接口从devkit AICore Hcomm迁移而来，当前文档按迁移后的ROCE和UBC_CTP/URMA实现约束描述。

## 模板参数

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| 参数 | 说明 |
| --- | --- |
| `commProtocol` | 通信协议类型。当前实现覆盖`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UBC_CTP`，默认值为`COMM_PROTOCOL_UBC_CTP`。 |

## 协议能力

| 接口 | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | 支持，需要提供UB临时工作区。 | 支持，需要提供URMA临时工作区。 |
| `ReadNbi` | 支持。 | 支持。 |
| `WriteNbi` | 支持。 | 支持。 |
| `WriteWithNotifyNbi` | 不支持，调用会返回失败。 | 支持。 |
| `Commit` | 支持。 | 支持。 |
| `Drain` | 支持。 | 支持。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [Init](./Init.md) | 初始化Hcomm临时工作区。 |
| [ReadNbi](./ReadNbi.md) | 通过指定通道提交读任务。 |
| [WriteNbi](./WriteNbi.md) | 通过指定通道提交写任务。 |
| [WriteWithNotifyNbi](./WriteWithNotifyNbi.md) | 提交写任务并写通知值。 |
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
- `WriteWithNotifyNbi`当前仅支持`COMM_PROTOCOL_UBC_CTP`路径，`COMM_PROTOCOL_ROCE`路径会返回失败。
- 传入的`ChannelHandle`需要指向与协议匹配的通道实体。
