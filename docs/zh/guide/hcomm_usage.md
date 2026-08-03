# Hcomm使用说明

## 概述

Hcomm是asc-comm当前提供的AICore侧点对点通信接口。使用方通过`AscendC::Hcomm`模板选择通信协议，并通过`ChannelHandle`指定通信通道。当前仓库重点承载AIV直驱实现，覆盖RoCE和UBC_CTP/URMA两条路径。

## 基本流程

1. 包含头文件。

```cpp
#include "hcomm/hcomm.h"
```

2. 创建Hcomm对象。

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
```

3. 调用`Init`初始化临时工作区。

```cpp
int32_t ret = hcomm.Init(tmpBuf, tmpLen);
```

4. 提交通信任务。

```cpp
ret = hcomm.WriteNbi(channel, dst, src, len);
ret = hcomm.ReadNbi(channel, dst, src, len);
ret = hcomm.WriteWithNotifyNbi(channel, dst, src, len, notifyAddr, notifyVal);
ret = hcomm.AtomicFAA<uint64_t>(channel, remoteCounter, fetchAddr, addVal);
ret = hcomm.AtomicCAS<uint64_t>(channel, remoteValue, fetchAddr, compareVal, swapVal);
```

5. 如果提交任务时设置`commit = false`，需要显式调用`Commit`。

```cpp
ret = hcomm.WriteNbi<false>(channel, dst, src, len);
ret = hcomm.Commit(channel);
```

6. 调用`Drain`等待任务完成。

```cpp
ret = hcomm.Drain(channel);
```

## 协议说明

| 协议 | 说明 |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | RoCE点对点通信路径，支持`ReadNbi`、`WriteNbi`、`Commit`、`Drain`，不支持`WriteWithNotifyNbi`。 |
| `COMM_PROTOCOL_UBC_CTP` | UBC CTP/URMA路径，支持`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`、`AtomicCAS`、`Commit`、`Drain`；Ascend 950平台的AIV还支持`Lock`和`Unlock`。 |

## 多AI Core共享通道

Ascend 950平台上，多个AIV共享同一个`COMM_PROTOCOL_UBC_CTP`通道时，需要通过[Lock](../api/aicore/hcomm/Lock.md)和[Unlock](../api/aicore/hcomm/Unlock.md)保护会更新通道状态的访问。每个AI Core可以使用独立的Hcomm对象和UB临时工作区，但传入相同的`ChannelHandle`。

```cpp
int32_t lockRet = hcomm.Lock(channel);
if (lockRet == 0) {
    int32_t opRet = hcomm.WriteNbi(channel, dst, src, len);

    // 即使通信接口返回失败，也必须释放已经成功获取的锁。
    int32_t unlockRet = hcomm.Unlock(channel);
    // 分别处理opRet和unlockRet。
}
```

锁只同步同一设备上共享该通道的AI Core，不同步不同Rank或不同设备。`Unlock`只写回并失效通道counter所在的缓存行，不能替代任意用户GM数据所需的同步和cache维护操作。

## 注意事项

- `COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UBC_CTP`路径均需要先调用`Init`提供临时工作区，当前最小工作区大小为512字节。
- 使用`__ubuf__ uint8_t*`初始化时，实现会对临时工作区起始地址做32字节对齐；使用`LocalTensor`初始化时，调用方需要保证tensor容量满足工作区要求。
- `ChannelHandle`指向的通道实体由调用方负责初始化和维护。
- 源地址、目的地址和长度需要满足底层协议和硬件要求。
- `WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`仅支持`COMM_PROTOCOL_UBC_CTP`路径。
- 原子操作的数据类型仅支持`int32_t`、`uint32_t`、`int64_t`和`uint64_t`。
- `COMM_PROTOCOL_UBC_CTP`路径中，`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`单任务占用2个WQE block，普通`ReadNbi`/`WriteNbi`占用1个WQE block。
- `Lock`和`Unlock`仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径。调用前需要确保通道锁已初始化为FREE。
- 通道锁不可重入，没有超时和公平性保证。每次成功调用`Lock`后，所有退出路径都必须由获取锁的同一AI Core调用`Unlock`。
- 返回值为`0`表示成功，`-1`表示失败。

## 样例

可参考[hcomm_write_read_nbi](../../../examples/hcomm_write_read_nbi/README.md)了解AIV Kernel侧接口调用方式和Host侧通信资源创建流程。该样例固定使用`COMM_ENGINE_AIV`和`COMM_PROTOCOL_UBC_CTP`，不覆盖RoCE路径及`Lock`/`Unlock`接口。

该样例在两卡场景下对称执行`WriteNbi`和`ReadNbi`：

```cpp
hcomm.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);
hcomm.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);
hcomm.Drain(channel);
```

样例运行依赖Ascend 950PR/Ascend 950DT和至少2张NPU；单卡环境仅支持编译验证。
