# asccomm

## 功能说明

头文件为：

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::asccomm`是AICore侧点对点通信接口模板，支持通过通信通道提交读、写、写通知和原子操作任务，并通过`commit`和`drain`控制任务提交与完成等待。本文档说明RoCE和UBC_CTP/URMA路径的协议能力和使用约束。

## 模板参数

```cpp
template <AscendC::comm_protocol comm_protocol = AscendC::comm_protocol_ubc_ctp>
class asccomm;
```

| 参数 | 说明 |
| --- | --- |
| `comm_protocol` | 通信协议类型，支持`comm_protocol_roce`和`comm_protocol_ubc_ctp`，默认值为`comm_protocol_ubc_ctp`。 |

## 协议能力

| 接口 | `comm_protocol_roce` | `comm_protocol_ubc_ctp` |
| --- | --- | --- |
| `init` | 支持，需要提供UB临时工作区。 | 支持，需要提供URMA临时工作区。 |
| `read_nbi` | 支持。 | 支持。 |
| `write_nbi` | 支持。 | 支持。 |
| `write_with_notify_nbi` | 不支持，调用会返回失败。 | 支持。 |
| `atomic_faa` | 不支持。 | 支持。 |
| `atomic_cas` | 不支持。 | 支持。 |
| `commit` | 支持。 | 支持。 |
| `drain` | 支持。 | 支持。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [init](./Init.md) | 初始化asccomm临时工作区。 |
| [read_nbi](./ReadNbi.md) | 通过指定通道提交读任务。 |
| [write_nbi](./WriteNbi.md) | 通过指定通道提交写任务。 |
| [write_with_notify_nbi](./WriteWithNotifyNbi.md) | 提交写任务并写通知值。 |
| [atomic_faa](./AtomicFAA.md) | 提交Fetch-and-add原子操作任务。 |
| [atomic_cas](./AtomicCAS.md) | 提交Compare-and-swap原子操作任务。 |
| [commit](./Commit.md) | 显式提交通道上的待执行任务。 |
| [drain](./Drain.md) | 等待通道上的通信任务完成。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 执行成功。 |
| `-1` | 执行失败。 |

## 使用约束

- 调用通信接口前，通信通道需要由调用方完成初始化。
- `comm_protocol_roce`和`comm_protocol_ubc_ctp`路径均需要通过`init`提供临时工作区，不同协议使用的临时工作区布局不同。
- `write_with_notify_nbi`仅支持`comm_protocol_ubc_ctp`路径，`comm_protocol_roce`路径会返回失败。
- `atomic_faa`和`atomic_cas`仅支持`comm_protocol_ubc_ctp`路径，数据类型仅支持`int32_t`、`uint32_t`、`int64_t`、`uint64_t`。
- 传入的`channel_handle`需要指向与协议匹配的通道实体。

## 相关样例

[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README.md)演示两卡场景下，AIV Kernel通过`comm_protocol_ubc_ctp`路径调用`write_nbi`和`read_nbi`。该样例不覆盖RoCE路径。
