# API参考

## AICore Hcomm

| 文档 | 内容 |
| --- | --- |
| [Hcomm](./aicore/hcomm/Hcomm.md) | AICore侧点对点通信接口模板总览、协议能力和使用约束。 |
| [Init](./aicore/hcomm/Init.md) | 初始化Hcomm临时工作区。 |
| [read_nbi](./aicore/hcomm/ReadNbi.md) | 通过指定通道提交点对点读任务。 |
| [write_nbi](./aicore/hcomm/WriteNbi.md) | 通过指定通道提交点对点写任务。 |
| [write_with_notify_nbi](./aicore/hcomm/WriteWithNotifyNbi.md) | 提交写任务并写远端通知值。 |
| [atomic_faa](./aicore/hcomm/AtomicFAA.md) | 提交Fetch-and-add原子操作任务。 |
| [atomic_cas](./aicore/hcomm/AtomicCAS.md) | 提交Compare-and-swap原子操作任务。 |
| [commit](./aicore/hcomm/Commit.md) | 显式提交通道上的待执行通信任务。 |
| [drain](./aicore/hcomm/Drain.md) | 等待通道上的通信任务完成。 |

## AICore ain

### ain类

| 文档 | 内容 |
| --- | --- |
| [ain](./aicore/ain/Ain.md) | AICore侧AIN单边通信接口模板总览、协议能力和使用约束。 |
| [put](./aicore/ain/Put.md) | 将本端对称window中的数据写入对端对称window。 |
| [put_value](./aicore/ain/PutValue.md) | 将立即数写入对端对称window。 |
| [get](./aicore/ain/Get.md) | 从对端对称window读取数据到本端对称window。 |
| [flush](./aicore/ain/Flush.md) | 等待team内所有peer通道上的通信任务完成。 |
| [flush_async](./aicore/ain/FlushAsync.md) | 获取指定peer的通信通道句柄，用于后续异步等待。 |
| [wait](./aicore/ain/Wait.md) | 等待指定通信通道上的任务完成。 |
| [signal](./aicore/ain/Signal.md) | 对远端signal执行原子加操作。 |
| [read_signal](./aicore/ain/ReadSignal.md) | 读取本端signal值。 |
| [wait_signal](./aicore/ain/WaitSignal.md) | 等待本端signal达到指定阈值。 |

### ain_barrier_session类

| 文档 | 内容 |
| --- | --- |
| [ain_barrier_session](./aicore/ain/AinBarrierSession.md) | AICore侧AIN barrier同步会话总览。 |
| [sync](./aicore/ain/Sync.md) | 执行team内barrier同步。 |

## 头文件

```cpp
#include "hcomm/hcomm.h"
#include "ain/ain.h"
```

## 相关文档

- [Hcomm使用说明](../guide/hcomm_usage.md)
- [AIV直驱URMA write_nbi/read_nbi样例](../../../examples/hcomm_write_read_nbi/README.md)
- [ain Basic Ring样例](../../../examples/ain/basic_ring/README.md)
