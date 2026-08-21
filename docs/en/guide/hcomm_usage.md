# Hcomm Usage Guide

## Overview

Hcomm is the AICore-side point-to-point communication interface provided by asc-comm. Users select a communication protocol through the `AscendC::Hcomm` template. Ordinary interfaces submit tasks individually through a `ChannelHandle`. The UBC_CTP/URMA path on Ascend 950 also supports BatchHandle, which prepares WQEs in UB before copying and submitting them to the GM SQ together.

## Ordinary Interface Workflow

1. Include the header file.

```cpp
#include "hcomm/hcomm.h"
```

2. Instantiate the Hcomm object.

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
```

3. Call `Init` to initialize the temporary workspace used by ordinary interfaces.

```cpp
int32_t ret = hcomm.Init(tmpBuf, tmpLen);
```

4. Submit communication tasks.

```cpp
ret = hcomm.WriteNbi(channel, dst, src, len);
ret = hcomm.ReadNbi(channel, dst, src, len);
ret = hcomm.WriteWithNotifyNbi(channel, dst, src, len, notifyAddr, notifyVal);
ret = hcomm.AtomicFAA<uint64_t>(channel, remoteCounter, fetchAddr, addVal);
ret = hcomm.AtomicCAS<uint64_t>(channel, remoteValue, fetchAddr, compareVal, swapVal);
```

5. If `commit = false` is set during task submission, explicitly call `Commit`.

```cpp
ret = hcomm.WriteNbi<false>(channel, dst, src, len);
ret = hcomm.Commit(channel);
```

6. Call ordinary `Drain` to wait for task completion.

```cpp
ret = hcomm.Drain(channel);
```

## BatchHandle Interface Workflow

BatchHandle interfaces currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950 and do not depend on `Init`.

1. Prepare a UB buffer and create a `UbcCtpBatchHandle`. Use `auto` to receive the return value.

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> batchBuffer = batchTBuf.Get<uint8_t>();
auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteAddr, localAddr);
```

A non-null `remoteAddr` must fall within a remote registered buffer of the channel. When it is null, the interface selects remote registered buffer 0. This is the only remote registration lookup: the interface caches the selected buffer's `tokenId/tokenValue`, and subsequent batch operations do not validate their remote address ranges. `localAddr` is reserved in the current version.

2. Prepare WQEs in the UB buffer of the BatchHandle. Read, write, and write-with-notify tasks can be mixed in the same batch.

```cpp
int32_t ret = hcomm.WriteNbi(batchHandle, remoteWriteDst, localWriteSrc, writeLen);
ret = hcomm.ReadNbi(batchHandle, localReadDst, remoteReadSrc, readLen);
ret = hcomm.WriteWithNotifyNbi(
    batchHandle, remoteNotifyDst, localNotifySrc, notifyLen, notifyAddr, notifyVal);
```

These calls only prepare WQEs. They do not copy to the GM SQ or ring the doorbell. The caller must ensure that the remote Write/WriteWithNotify destination ranges, the remote Read source ranges, and `notifyAddr` all belong to the registered memory represented by the `tokenId/tokenValue` cached when the handle was created.

3. Call `BatchCommit` to copy and submit the current batch. A successful call always rings the SQ doorbell.

```cpp
ret = hcomm.BatchCommit(batchHandle);
```

After a successful commit, the BatchHandle and UB buffer can prepare another batch. Multiple `BatchCommit` calls are allowed, but the caller must ensure that accumulated outstanding tasks do not overwrite WQEs in the SQ that have not yet been consumed. CQEs generated but not consumed since the previous batch `Drain` must not exceed the CQ capacity. Submission does not poll the CQ automatically.

4. Call batch `Drain` to wait for submitted tasks.

```cpp
ret = hcomm.Drain(batchHandle);
```

Batch `Drain` reuses the first 64 bytes of the BatchHandle WQE buffer as CQE scratch space, so it does not require `Init`. The BatchHandle must not contain WQEs that have not been submitted through `BatchCommit`.

## Batch Buffer and CQE Configuration

- Each UBC_CTP WQEBB is 64 bytes. Batch `ReadNbi` and `WriteNbi` each occupy one WQEBB, while batch `WriteWithNotifyNbi` occupies two.
- The buffer WQEBB capacity is `batchBufferLen / 64`. A batch requires "number of reads + number of writes + 2 x number of write-with-notify tasks" WQEBBs.
- `batchBufferLen` must be at least 64 bytes and strictly smaller than `sqDepth * 64` bytes.
- Batch interfaces require `config.inlineEn = 0`. `config.cqe` can be `0` or `1`, and both settings can be mixed in one batch.
- The default `URMA_DEFAULT_CFG` generates a CQE for every request. If all submitted requests use `cqe = 0`, batch `Drain` has no CQE to poll and returns directly; this does not confirm that hardware has completed those requests.
- If ordering and fence settings guarantee that the final request completes after all preceding requests, only the final request needs `cqe = 1`, followed by one batch `Drain`.

## Protocol Description

| Protocol | Description |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | Supports ordinary `ReadNbi`, `WriteNbi`, `Commit`, and `Drain`. BatchHandle interfaces are not supported. |
| `COMM_PROTOCOL_UBC_CTP` | Supports ordinary read, write, write-with-notify, atomic, commit, and drain interfaces. Ascend 950 also supports batch read, write, write-with-notify, `BatchCommit`, and batch `Drain`. |

## Notes

- Ordinary interfaces require a temporary workspace allocated through `Init`. The current minimum size is 512 bytes. BatchHandle interfaces do not require `Init`.
- When `Init` uses `__ubuf__ uint8_t*`, the implementation aligns the start address of the temporary workspace to 32 bytes. With `LocalTensor`, the caller must ensure sufficient tensor capacity.
- The UB buffer passed to `MakeBatchHandle` must start at a 32-byte aligned address. The caller manages its capacity and lifetime.
- The caller is responsible for initializing and maintaining the channel entity referenced by `ChannelHandle`.
- A BatchHandle caches SQ/CQ contexts and queue counters at creation time. The caller must exclusively own the channel while using it. Do not mix ordinary interfaces on the same channel or use multiple BatchHandles concurrently.
- A single transfer through a batch interface must not exceed `UINT32_MAX` bytes.
- The ordinary `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` interfaces are only available for the `COMM_PROTOCOL_UBC_CTP` path.
- Supported data types for atomic operations are limited to `int32_t`, `uint32_t`, `int64_t`, and `uint64_t`.

## Sample

Refer to [hcomm_write_read_nbi](../../../examples/hcomm_write_read_nbi/README_en.md) for the ordinary AIV Kernel-side API workflow and Host-side communication resource creation. This sample uses `COMM_ENGINE_AIV` and `COMM_PROTOCOL_UBC_CTP` and does not cover the RoCE or BatchHandle workflow.

The sample executes ordinary `WriteNbi` and `ReadNbi` symmetrically in a two-card scenario:

```cpp
hcomm.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);
hcomm.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);
hcomm.Drain(channel);
```

The sample requires Ascend 950PR/Ascend 950DT and at least two NPUs for runtime verification. Single-NPU environments only support compilation checks.
