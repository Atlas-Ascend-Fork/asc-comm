# API参考

## Host Hcomm

| 文档 | 内容 |
| --- | --- |
| [MakeMultiChannelHandle](./host/hcomm/MakeMultiChannelHandle.md) | 基于HCCL通信域或Hcomm Endpoint创建批量接口所需的多通道资源。 |
| [DestroyMultiChannelHandle](./host/hcomm/DestroyMultiChannelHandle.md) | 释放Hcomm Endpoint路径创建的多通道资源。 |

## AICore Hcomm

| 文档 | 内容 |
| --- | --- |
| [Hcomm](./aicore/hcomm/Hcomm.md) | AICore侧点对点通信接口模板总览、协议能力和使用约束。 |
| [Init](./aicore/hcomm/Init.md) | 初始化普通Hcomm接口使用的临时工作区。 |
| [MakeBatchHandle](./aicore/hcomm/MakeBatchHandle.md) | 创建批量句柄并绑定批量操作所需的UB工作区。 |
| [GetHandleRef](./aicore/hcomm/GetHandleRef.md) | 从多通道批量句柄中选择逻辑通道。 |
| [Lock](./aicore/hcomm/Lock.md) | 获取通信通道的跨AI Core锁。 |
| [Unlock](./aicore/hcomm/Unlock.md) | 释放通信通道的跨AI Core锁。 |
| [ReadNbi](./aicore/hcomm/ReadNbi.md) | 提交普通读任务或向BatchHandle添加读任务。 |
| [WriteNbi](./aicore/hcomm/WriteNbi.md) | 提交普通写任务或向BatchHandle添加写任务。 |
| [WriteValueNbi](./aicore/hcomm/WriteValueNbi.md) | 提交点对点立即数写任务，将`value`内联携带在WQE中写入远端。 |
| [WriteWithNotifyNbi](./aicore/hcomm/WriteWithNotifyNbi.md) | 提交普通写通知任务或向BatchHandle添加写通知任务。 |
| [AtomicFAA](./aicore/hcomm/AtomicFAA.md) | 提交Fetch-and-add原子操作任务。 |
| [AtomicCAS](./aicore/hcomm/AtomicCAS.md) | 提交Compare-and-swap原子操作任务。 |
| [Commit](./aicore/hcomm/Commit.md) | 显式提交通道上的待执行通信任务。 |
| [BatchCommit](./aicore/hcomm/BatchCommit.md) | 提交BatchHandle中的全部任务。 |
| [Drain](./aicore/hcomm/Drain.md) | 等待普通通道或BatchHandle已提交的通信任务完成。 |

## AICore Jetty

| 文档 | 内容 |
| --- | --- |
| [Jetty](./aicore/jetty/Jetty.md) | AICore侧低层Jetty通信接口总览，覆盖SIMD与SIMT路径及混合发布模式。 |
| [Write](./aicore/jetty/Write.md) | 提交URMA写WQE，SIMT路径支持协作组构造与多SGE。 |
| [WriteValue](./aicore/jetty/WriteValue.md) | 提交内联立即数写WQE。 |
| [WriteWithNotify](./aicore/jetty/WriteWithNotify.md) | 提交写WQE并在完成时向远端写入通知值。 |
| [Drain](./aicore/jetty/Drain.md) | 轮询CQ等待已提交WQE完成。 |
| [AdvanceSq](./aicore/jetty/AdvanceSq.md) | 推进SQ头，延迟提交路径的组成部分。 |
| [PublishSq](./aicore/jetty/PublishSq.md) | 写入DWQE窗口发布队列，SIMT延迟提交路径的组成部分。 |
| [RingDoorbell](./aicore/jetty/RingDoorbell.md) | 敲SQ门铃发布已入队WQE，支持SIMT构造/SIMD发布混合模式。 |

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

Host侧：

```cpp
#include "hcomm/hcomm_host.h"
```

AICore侧：

```cpp
#include "hcomm/hcomm.h"
#include "ain/ain.h"
#include "jetty/hcomm_jetty.h"
#include "jetty/hcomm_jetty_simt.h"
```

## 相关文档

- [Hcomm使用说明](../guide/hcomm_usage.md)
- [AIV直驱URMA WriteNbi/ReadNbi样例](../../../examples/aicore/hcomm/01_hcomm_write_read_nbi/README.md)
- [Ain Basic Ring样例](../../../examples/aicore/ain/01_basic_ring/README.md)
- [SIMT Jetty WRITE样例](../../../examples/simt_jetty/README.md)
- [AIV Jetty WRITE样例](../../../examples/hcomm_jetty_write/README.md)
