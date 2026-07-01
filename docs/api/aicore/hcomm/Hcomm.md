# Hcomm

## 功能说明

头文件为：

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm` 是 AICore 侧点对点通信接口模板，支持通过通信通道提交读写任务，并通过 `Commit` 和 `Drain` 控制任务提交与完成等待。

## 模板参数

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| 参数 | 说明 |
| --- | --- |
| `commProtocol` | 通信协议类型。当前实现覆盖 `COMM_PROTOCOL_ROCE` 和 `COMM_PROTOCOL_UBC_CTP`。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [Init](./Init.md) | 初始化 Hcomm 临时工作区。 |
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
- `COMM_PROTOCOL_UBC_CTP` 路径需要通过 `Init` 提供临时工作区。
- 传入的 `ChannelHandle` 需要指向与协议匹配的通道实体。
