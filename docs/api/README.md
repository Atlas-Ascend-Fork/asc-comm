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

## 头文件

```cpp
#include "hcomm/hcomm.h"
```

## 相关文档

- [Hcomm使用说明](../guide/hcomm_usage.md)
- [AIV直驱URMA WriteNbi/ReadNbi样例](../../examples/hcomm_write_read_nbi/README.md)
