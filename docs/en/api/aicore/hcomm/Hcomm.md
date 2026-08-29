# Hcomm

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Supports the RoCE protocol but not the URMA protocol
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Supports the RoCE protocol but not the URMA protocol
- Atlas inference series products Vector Core: Supports the RoCE protocol but not the URMA protocol
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Supports the RoCE protocol but not the URMA protocol
<!-- end id7 -->

## Function Description

The header file is:

```cpp
#include "hcomm/hcomm.h"
```

`AscendC::Hcomm` is a point-to-point communication interface template on the AICore side. Ordinary interfaces submit individual read, write, write-with-notify, and atomic operation tasks through a `ChannelHandle`, and use `Commit` and `Drain` to control task submission and completion waiting. The UBC_CTP path also provides BatchHandle interfaces, which support both single-channel batch submission and preparing WQEs for multiple logical channels on a shared Jetty through `MultiChannelHandle`, followed by a single `BatchCommit` submission.

## Function Prototype

```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `commProtocol` | Communication protocol type. Supports `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP`. Default: `COMM_PROTOCOL_UBC_CTP`. |

`MakeBatchHandle` derives its return type through compile-time traits: `ChannelHandle` maps to `UbcCtpBatchHandle`, and `MultiChannelHandle` maps to `UbcCtpMultiBatchHandle`. In multi-channel mode, batch read and write are performed through the inner `UbcCtpBatchHandle` reference returned by `GetHandleRef`, and `BatchCommit` and batch `Drain` are performed through the outer `UbcCtpMultiBatchHandle`. Batch interfaces support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.

## Protocol Capabilities

| Interface | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | Supported; provides the UB temporary workspace for ordinary interfaces. | Supported; provides the URMA temporary workspace for ordinary interfaces. |
| `Lock` | Not supported. | Supported, only by AIV on the Ascend 950 platform. |
| `Unlock` | Not supported. | Supported, only by AIV on the Ascend 950 platform. |
| Ordinary `ReadNbi` | Supported. | Supported. |
| Ordinary `WriteNbi` | Supported. | Supported. |
| Ordinary `WriteValueNbi` | Not supported; invocation returns failure. | Supported. |
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

| Interface | Description |
| --- | --- |
| [Init](./Init.md) | Initialize the temporary workspace used by ordinary Hcomm interfaces. |
| [MakeBatchHandle](./MakeBatchHandle.md) | Create a batch handle and bind the UB buffer for batch WQEs. |
| [GetHandleRef](./GetHandleRef.md) | Select a logical channel from a shared-Jetty multi-channel batch handle. |
| [Lock](./Lock.md) | Acquire the cross-AI-Core lock of a communication channel. |
| [Unlock](./Unlock.md) | Release the cross-AI-Core lock of a communication channel. |
| [ReadNbi](./ReadNbi.md) | Submit a read task through an ordinary channel, or prepare a read WQE in a BatchHandle. |
| [WriteNbi](./WriteNbi.md) | Submit a write task through an ordinary channel, or prepare a write WQE in a BatchHandle. |
| [WriteValueNbi](./WriteValueNbi.md) | Submit an immediate-value write task through an ordinary channel, carrying `value` inline in the WQE to write to the remote side. |
| [WriteWithNotifyNbi](./WriteWithNotifyNbi.md) | Submit a write-with-notify task through an ordinary channel, or prepare a write-with-notify WQE in a BatchHandle. |
| [AtomicFAA](./AtomicFAA.md) | Submit a Fetch-and-add atomic operation task. |
| [AtomicCAS](./AtomicCAS.md) | Submit a Compare-and-swap atomic operation task. |
| [Commit](./Commit.md) | Explicitly submit pending tasks on an ordinary channel. |
| [BatchCommit](./BatchCommit.md) | Submit all WQEs prepared in a BatchHandle. |
| [Drain](./Drain.md) | Wait for tasks submitted through an ordinary channel or BatchHandle to complete. |

## Return Value

Interfaces that return a status code generally use `0` for success and `-1` for failure. `Drain` may also return an underlying CQ polling error code. `MakeBatchHandle` directly returns a protocol-specific batch handle instead of a status code. See the corresponding API document for the specific return values of each interface.

## Constraints

- Communication channel resources must be initialized by the caller before communication interfaces are invoked.
- Ordinary `ChannelHandle` interfaces require a temporary workspace provided through `Init` first. The BatchHandle workflow does not depend on `Init`; its UB workspace is provided by `MakeBatchHandle`.
- `WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` support only the `COMM_PROTOCOL_UBC_CTP` path.
- Batch interfaces currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Ordinary and batch interfaces use different queue state management. A BatchHandle caches the SQ/CQ contexts and queue counters at creation time. While it is in use, the caller must exclusively own the corresponding single channel or shared Jetty and must not mix ordinary interfaces or use multiple BatchHandles concurrently.
- The passed `ChannelHandle` must point to a channel entity that matches the protocol.
- If the `commit` template parameter of an ordinary interface is set to `false`, the number of consecutive calls must not exceed `sqDepth`. Pending tasks must be committed through `Commit` or automatic commit before the SQ is exhausted; otherwise subsequent `PostSend` fails due to SQ overflow.
- In the batched-submission scenario of ordinary interfaces (multiple delayed commits + one final commit), only the last commit should produce a CQE (that is, `config.cqe` is set to `0` for intermediate tasks and `1` for the last one).

See [MakeMultiChannelHandle](../../host/hcomm/MakeMultiChannelHandle.md) for the Host-side handle creation interface of a shared Jetty.

## Example

The calling example of ordinary Hcomm interfaces is as follows:

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
pipe->InitBuffer(hcommBuf, HCOMM_WORKSPACE_SIZE);
auto hcommTensor = hcommBuf.Get<uint8_t>();
hcomm.Init(hcommTensor, HCOMM_WORKSPACE_SIZE);
hcomm.WriteNbi(channel, writeDst, writeSrc, DATA_SIZE);
hcomm.Drain(channel);
```
