# Hcomm

## Function Description

Header file:

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm` is a point-to-point communication interface template on the AICore side. Ordinary interfaces use a `ChannelHandle` to submit individual read, write, write-with-notify, and atomic tasks, and use `Commit` and `Drain` to control submission and completion. The UBC_CTP path also provides BatchHandle interfaces for single-channel submission and for preparing WQEs for multiple logical channels on a shared Jetty through `MultiChannelHandle` before submitting them together with `BatchCommit`.

## Template Parameters

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| Parameter | Description |
| --- | --- |
| `commProtocol` | Communication protocol type. Supports `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP`. Default: `COMM_PROTOCOL_UBC_CTP`. |

`MakeBatchHandle` derives its return type through compile-time traits. `ChannelHandle` maps to `UbcBatchHandle`, while `MultiChannelHandle` maps to `UbcMultiBatchHandle`. In multi-channel mode, batch read/write uses the inner `UbcBatchHandle` reference returned by `GetHandleRef`, while `BatchCommit` and batch `Drain` use the outer `UbcMultiBatchHandle`. Batch interfaces support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

## Protocol Capabilities

| API | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | Supported for the UB temporary workspace used by ordinary interfaces. | Supported for the URMA temporary workspace used by ordinary interfaces. |
| `Lock` | Not supported. | Supported only by AIV on Ascend 950. |
| `Unlock` | Not supported. | Supported only by AIV on Ascend 950. |
| Ordinary `ReadNbi` | Supported. | Supported. |
| Ordinary `WriteNbi` | Supported. | Supported. |
| Ordinary `WriteWithNotifyNbi` | Not supported; invocation returns failure. | Supported. |
| `AtomicFAA` | Not supported. | Supported. |
| `AtomicCAS` | Not supported. | Supported. |
| Ordinary `Commit` | Supported. | Supported. |
| Ordinary `Drain` | Supported. | Supported. |
| `MakeBatchHandle` | Not supported. | Supported on Ascend 950. |
| `GetHandleRef` | Not supported. | Supported by the shared-Jetty batch path on Ascend 950. |
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
| [GetHandleRef](./GetHandleRef.md) | Select a logical channel from a shared-Jetty multi-channel batch handle. |
| [Lock](./Lock.md) | Acquire the cross-AI-Core lock of a communication channel. |
| [Unlock](./Unlock.md) | Release the cross-AI-Core lock of a communication channel. |
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
- Ordinary and batch interfaces manage queue state differently. A BatchHandle caches SQ/CQ contexts and counters at creation time. The caller must exclusively own the single channel or shared Jetty while using it and must not mix ordinary calls or use multiple BatchHandles concurrently.
- The passed `ChannelHandle` must point to a channel entity matching the selected protocol.

See [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md) for the Host-side shared-Jetty handle creation API. It provides overloads for an HCCL communicator and an Hcomm Endpoint.

## Related Sample

[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README_en.md) demonstrates ordinary `WriteNbi` and `ReadNbi` calls over the `COMM_PROTOCOL_UBC_CTP` path in a two-card scenario. It does not cover the RoCE or BatchHandle workflow.
