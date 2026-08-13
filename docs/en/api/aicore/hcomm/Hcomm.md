# Hcomm

## Function Description

Header file:

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm` is a point-to-point communication interface template on the AICore side. Ordinary interfaces use a `ChannelHandle` to submit individual read, write, write-with-notify, and atomic tasks, and use `Commit` and `Drain` to control submission and completion. The UBC_CTP path also provides BatchHandle interfaces that prepare read, write, and write-with-notify WQEs in UB before submitting them together through `BatchCommit`.

## Template Parameters

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| Parameter | Description |
| --- | --- |
| `commProtocol` | Communication protocol type. Supports `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP`. Default: `COMM_PROTOCOL_UBC_CTP`. |

Batch interfaces derive and constrain their handle types through compile-time traits. Currently, only the
`ChannelHandle` to `UbcCtpBatchHandle` mapping is provided, and the interfaces support only the
`COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

## Protocol Capabilities

| API | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | Supported for the UB temporary workspace used by ordinary interfaces. | Supported for the URMA temporary workspace used by ordinary interfaces. |
| Ordinary `ReadNbi` | Supported. | Supported. |
| Ordinary `WriteNbi` | Supported. | Supported. |
| Ordinary `WriteWithNotifyNbi` | Not supported; invocation returns failure. | Supported. |
| `AtomicFAA` | Not supported. | Supported. |
| `AtomicCAS` | Not supported. | Supported. |
| Ordinary `Commit` | Supported. | Supported. |
| Ordinary `Drain` | Supported. | Supported. |
| `MakeBatchHandle` | Not supported. | Supported on Ascend 950. |
| Batch `ReadNbi` | Not supported. | Supported on Ascend 950. |
| Batch `WriteNbi` | Not supported. | Supported on Ascend 950. |
| Batch `WriteWithNotifyNbi` | Not supported. | Supported on Ascend 950. |
| `BatchCommit` | Not supported. | Supported on Ascend 950. |
| Batch `Drain` | Not supported. | Supported on Ascend 950. |

## Common APIs

| API | Description |
| --- | --- |
| [Init](./Init.md) | Initialize the temporary workspace used by ordinary Hcomm interfaces. |
| [MakeBatchHandle](./MakeBatchHandle.md) | Create a batch handle and bind its UB buffer for batched WQEs. |
| [ReadNbi](./ReadNbi.md) | Submit an ordinary read task or prepare a read WQE in a BatchHandle. |
| [WriteNbi](./WriteNbi.md) | Submit an ordinary write task or prepare a write WQE in a BatchHandle. |
| [WriteWithNotifyNbi](./WriteWithNotifyNbi.md) | Submit an ordinary write-with-notify task or prepare one in a BatchHandle. |
| [AtomicFAA](./AtomicFAA.md) | Submit a Fetch-and-add atomic operation task. |
| [AtomicCAS](./AtomicCAS.md) | Submit a Compare-and-swap atomic operation task. |
| [Commit](./Commit.md) | Explicitly submit pending tasks on an ordinary channel. |
| [BatchCommit](./BatchCommit.md) | Submit all WQEs prepared in a BatchHandle. |
| [Drain](./Drain.md) | Wait for tasks submitted through an ordinary channel or BatchHandle to complete. |

## Return Value

Interfaces that return a status code generally use `0` for success and `-1` for failure. `Drain` may also return an underlying CQ polling error code. `MakeBatchHandle` returns a protocol-specific batch handle directly instead of a status code. See the corresponding API document for exact return values.

## Usage Constraints

- Communication channel resources must be initialized by the caller before communication APIs are invoked.
- Ordinary `ChannelHandle` interfaces require a temporary workspace provided through `Init`. The BatchHandle workflow does not require `Init`; `MakeBatchHandle` provides its UB workspace.
- The ordinary overloads of `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` are supported only on the `COMM_PROTOCOL_UBC_CTP` path.
- Batch interfaces currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Ordinary and batch interfaces manage queue state differently. A BatchHandle caches SQ/CQ contexts and counters at creation time. The caller must exclusively own the channel while using it, and must not mix ordinary calls or use multiple BatchHandles concurrently on the same channel.
- The passed `ChannelHandle` must point to a channel entity matching the selected protocol.

## Related Sample

[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README_en.md) demonstrates ordinary `WriteNbi` and `ReadNbi` calls over the `COMM_PROTOCOL_UBC_CTP` path in a two-card scenario. It does not cover the RoCE or BatchHandle workflow.
