# Write

## Overview

Submits a URMA write WQE to the Jetty SQ. The SIMD path submits a single-WQE write task; the SIMT path supports thread/warp/group cooperative WQE construction with up to 12 SGEs per WQE.

## Prototype

SIMD path:

```cpp
template <int64_t timeoutCycles, bool doCommit = true>
__aicore__ inline int32_t Write(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes);
```

SIMT path:

```cpp
template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
__simt_callee__ __forceinline__ int32_t Write(
    const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const Coop& coop = Coop{}) const;
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `peer` | Input | Remote metadata resolved by `HcommPeer`. |
| `dstAddr` / `dst` | Output | The remote destination GM address. On the SIMT path each SGE carries its own destination. |
| `srcAddr` | Input | The local source GM address (SIMD path). |
| `numBytes` | Input | The write length in bytes (SIMD path). |
| `sges` | Input | The SGE array; each SGE carries one local source address and length (SIMT path). |
| `coop` | Input | The cooperative-group object: thread/warp/group (SIMT path). |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `timeoutCycles` | The wait cycle budget while polling CQEs (SIMD path). |
| `doCommit` / `kDoCommit` | Whether to publish immediately on submission. `false` puts the task on the deferred path. |
| `kSgeNum` | The number of SGEs carried by the WQE, in the range 1 to 12 (SIMT path). |
| `Coop` | The cooperative-group type, defaulting to `HcommCoopThread` (SIMT path). |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The WQE was submitted successfully. |
| `-1` | The operation failed, including insufficient SQ space. |

## Constraints

- Only the `COMM_PROTOCOL_UB_CTP` path on Ascend 950 is supported.
- `dst` must fall within the remote buffer range registered with the channel.
- On the SIMT path, SGEs with a zero address or length do not count toward the WRITE; valid SGEs are compacted.
- With `kDoCommit = false`, `AdvanceSq` and `PublishSq` must follow to complete the publication.
- In warp-cooperative construction each lane contributes one SGE through shuffles, and the SGE order within the warp matches the lane index.
