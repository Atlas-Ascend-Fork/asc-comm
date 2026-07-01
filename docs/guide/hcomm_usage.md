# Hcomm 使用说明

## 概述

Hcomm 是 asc-comm 当前提供的 AICore 侧点对点通信接口。使用方通过 `AscendC::Hcomm` 模板选择通信协议，并通过 `ChannelHandle` 指定通信通道。接口行为参考 devkit AICore Hcomm 迁移实现，当前主要覆盖 ROCE 和 UBC_CTP/URMA 两条路径。

## 基本流程

1. 包含头文件。

```cpp
#include "hcomm/hcomm.h"
```

2. 创建 Hcomm 对象。

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
```

3. 调用 `Init` 初始化临时工作区。

```cpp
int32_t ret = hcomm.Init(tmpBuf, tmpLen);
```

4. 提交通信任务。

```cpp
ret = hcomm.WriteNbi(channel, dst, src, len);
ret = hcomm.ReadNbi(channel, dst, src, len);
ret = hcomm.WriteWithNotifyNbi(channel, dst, src, len, notifyAddr, notifyVal);
```

5. 如果提交任务时设置 `commit = false`，需要显式调用 `Commit`。

```cpp
ret = hcomm.WriteNbi<false>(channel, dst, src, len);
ret = hcomm.Commit(channel);
```

6. 调用 `Drain` 等待任务完成。

```cpp
ret = hcomm.Drain(channel);
```

## 协议说明

| 协议 | 说明 |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | RoCE 点对点通信路径，支持 `ReadNbi`、`WriteNbi`、`Commit`、`Drain`，不支持 `WriteWithNotifyNbi`。 |
| `COMM_PROTOCOL_UBC_CTP` | UBC CTP/URMA 路径，支持 `ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`Commit`、`Drain`。 |

## 注意事项

- `COMM_PROTOCOL_ROCE` 和 `COMM_PROTOCOL_UBC_CTP` 路径均需要先调用 `Init` 提供临时工作区。
- `ChannelHandle` 指向的通道实体由调用方负责初始化和维护。
- 源地址、目的地址和长度需要满足底层协议和硬件要求。
- 返回值为 `0` 表示成功，`-1` 表示失败。

## 样例

可参考 [AICore Hcomm 点对点通信 Kernel 样例](../../examples/aicore/hcomm_p2p_kernel/README.md) 了解 Kernel 侧接口调用方式。完整算子工程需要结合 HCOMM/HCCL control-plane 资源创建后使用，集成边界和外部依赖见 [完整算子集成说明](../../examples/aicore/hcomm_p2p_kernel/INTEGRATION.md)。
