# API参考

## AICore Hcomm

| 文档 | 内容 |
| --- | --- |
| [Hcomm](./aicore/hcomm/Hcomm.md) | AICore侧点对点通信接口模板总览、协议能力和使用约束。 |
| [Init](./aicore/hcomm/Init.md) | 初始化Hcomm临时工作区。 |
| [ReadNbi](./aicore/hcomm/ReadNbi.md) | 通过指定通道提交点对点读任务。 |
| [WriteNbi](./aicore/hcomm/WriteNbi.md) | 通过指定通道提交点对点写任务。 |
| [WriteWithNotifyNbi](./aicore/hcomm/WriteWithNotifyNbi.md) | 提交写任务并写远端通知值。 |
| [AtomicFAA](./aicore/hcomm/AtomicFAA.md) | 提交Fetch-and-add原子操作任务。 |
| [AtomicCAS](./aicore/hcomm/AtomicCAS.md) | 提交Compare-and-swap原子操作任务。 |
| [Commit](./aicore/hcomm/Commit.md) | 显式提交通道上的待执行通信任务。 |
| [Drain](./aicore/hcomm/Drain.md) | 等待通道上的通信任务完成。 |

## AICore Ain

### Ain类

| 文档 | 内容 |
| --- | --- |
| [Ain](./aicore/ain/Ain.md) | AICore侧AIN单边通信接口模板总览、协议能力和使用约束。 |
| [Put](./aicore/ain/Put.md) | 将本端对称window中的数据写入对端对称window。 |
| [PutValue](./aicore/ain/PutValue.md) | 将立即数写入对端对称window。 |
| [Get](./aicore/ain/Get.md) | 从对端对称window读取数据到本端对称window。 |
| [Flush](./aicore/ain/Flush.md) | 等待team内所有peer通道上的通信任务完成。 |
| [FlushAsync](./aicore/ain/FlushAsync.md) | 获取指定peer的通信通道句柄，用于后续异步等待。 |
| [Wait](./aicore/ain/Wait.md) | 等待指定通信通道上的任务完成。 |
| [Signal](./aicore/ain/Signal.md) | 对远端signal执行原子加操作。 |
| [ReadSignal](./aicore/ain/ReadSignal.md) | 读取本端signal值。 |
| [WaitSignal](./aicore/ain/WaitSignal.md) | 等待本端signal达到指定阈值。 |

### AinBarrierSession类

| 文档 | 内容 |
| --- | --- |
| [AinBarrierSession](./aicore/ain/AinBarrierSession.md) | AICore侧AIN barrier同步会话总览。 |
| [Sync](./aicore/ain/Sync.md) | 执行team内barrier同步。 |

## 头文件

```cpp
#include "hcomm/hcomm.h"
#include "ain/ain.h"
```

## 相关文档

- [Hcomm使用说明](../guide/hcomm_usage.md)
- [AIV直驱URMA WriteNbi/ReadNbi样例](../../../examples/hcomm_write_read_nbi/README.md)
- [Ain Basic Ring样例](../../../examples/ain/basic_ring/README.md)
