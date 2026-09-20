# Jetty

## Overview

Header files:

```cpp
// SIMD (AIV) path
#include "jetty/hcomm_jetty.h"
// SIMT (VF) path
#include "jetty/hcomm_jetty_simt.h"
```

`HcommJetty` is the low-level AICore-side Jetty communication interface. It builds and submits WQEs directly to the Jetty SQ, bypassing the task abstraction of the Hcomm layer. The SIMD path targets AIV kernels and submits individual WQEs through `HcommJetty::Write` and its siblings; the SIMT path targets SIMT VF kernels and supports thread/warp/group cooperative WQE construction.

`HcommPeer` resolves the remote queue and memory metadata from the GM Jetty table into user-allocated UB descriptors. SIMD and SIMT share the same 80B Jetty metadata and 48B peer descriptor layout, enabling the mixed mode where SIMT builds the WQEs and SIMD publishes them through `RingDoorbell`.

## Class Definition

SIMD path (`AscendC` namespace):

```cpp
class HcommJetty {
public:
    template <int64_t timeoutCycles, bool doCommit = true>
    __aicore__ inline int32_t Write(const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes);
    template <int64_t timeoutCycles, typename T, bool doCommit = true>
    __aicore__ inline int32_t WriteValue(const HcommPeer& peer, GM_ADDR dstAddr, T value);
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t WriteWithNotify(
        const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes,
        GM_ADDR notifyAddr, uint64_t notifyValue);
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t Drain();
    __aicore__ inline void AdvanceSq(uint32_t numWqebbs);
    __aicore__ inline void RingDoorbell();
};
```

The SIMT path (`AscendC::simt::jetty` namespace) adds cooperative-group interfaces on top: `Write` accepts a `Coop` cooperative-group parameter (thread/warp/group), and deferred submission is performed with `AdvanceSq` followed by `PublishSq`.

## Interface Capability

| Interface | SIMD | SIMT |
| --- | --- | --- |
| `Write` | Supported, single-WQE submission. | Supported, thread/warp/group cooperative construction, up to 12 SGEs. |
| `WriteValue` | Supported, value carried inline in one WQEBB. | Supported. |
| `WriteWithNotify` | Supported, writes a notify value to the remote side on completion. | Supported, single SGE. |
| `Drain` | Supported, polls the CQ until everything completes. | Supported, with a cooperative-group overload. |
| `AdvanceSq` | Supported, advances and publishes the SQ head. | Supported, used by the deferred path. |
| `RingDoorbell` | Supported, rings the SQ doorbell to publish enqueued WQEs. | Not applicable (SIMT publishes by writing the DWQE window through `PublishSq`). |
| `PublishSq` | Not applicable. | Supported, writes the 128B DWQE window to publish the queue. |

## Usage Constraints

- Only the `COMM_PROTOCOL_UB_CTP` path on Ascend 950 (`dav-3510`) is supported.
- One jetty maps to one communication channel; `HcommJetty` and `HcommPeer` cache their metadata in user-allocated UB space. A single jetty needs roughly 80B of metadata plus staging space for the largest WQE.
- The SIMT deferred path must call `Write<..., false>`, `AdvanceSq`, and `PublishSq` in order: `AdvanceSq` only advances the SQ head and marks the final WQE, while `PublishSq` writes the DWQE window that makes the hardware fetch.
- Concurrent `Write` calls on the same channel must belong to the same batch and are committed by the group leader; a group synchronization is required after all lanes finish constructing `HcommJetty` and before any warp starts to `Write`.
- In the mixed mode, the SIMD side may call `RingDoorbell` only after the SIMT side completes its WQE writes; the doorbell write from one core cannot order writes issued by another core.

## Interface List

| Interface | Description |
| --- | --- |
| [Write](./Write.md) | Submits a URMA write WQE; the SIMT path supports cooperative construction with multiple SGEs. |
| [WriteValue](./WriteValue.md) | Submits an inline immediate-value write WQE. |
| [WriteWithNotify](./WriteWithNotify.md) | Submits a write WQE that stores a notify value on the remote side at completion. |
| [Drain](./Drain.md) | Polls the CQ until the submitted WQEs complete. |
| [AdvanceSq](./AdvanceSq.md) | Advances the SQ head; part of the deferred submission path. |
| [PublishSq](./PublishSq.md) | Writes the DWQE window to publish the queue; part of the SIMT deferred path. |
| [RingDoorbell](./RingDoorbell.md) | Rings the SQ doorbell to publish enqueued WQEs, supporting the SIMT-build/SIMD-publish mixed mode. |
