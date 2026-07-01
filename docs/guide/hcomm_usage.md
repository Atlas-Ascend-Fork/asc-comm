# Hcomm 使用说明

## 概述

Hcomm 是 asc-comm 当前提供的 AICore 侧点对点通信接口。使用方通过 `AscendC::Hcomm` 模板选择通信协议，并通过 `ChannelHandle` 指定通信通道。

## 基本流程

1. 包含头文件。

```cpp
#include "hcomm/hcomm.h"
```

2. 创建 Hcomm 对象。

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
```

3. 如协议需要临时工作区，调用 `Init`。

```cpp
int32_t ret = hcomm.Init(tmpBuf, tmpLen);
```

4. 提交通信任务。

```cpp
ret = hcomm.WriteNbi(channel, dst, src, len);
ret = hcomm.ReadNbi(channel, dst, src, len);
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
| `COMM_PROTOCOL_ROCE` | RoCE 点对点通信路径。 |
| `COMM_PROTOCOL_UBC_CTP` | UBC CTP/URMA 路径，通常需要初始化临时工作区。 |

## 注意事项

- `ChannelHandle` 指向的通道实体由调用方负责初始化和维护。
- 源地址、目的地址和长度需要满足底层协议和硬件要求。
- 返回值为 `0` 表示成功，`-1` 表示失败。
