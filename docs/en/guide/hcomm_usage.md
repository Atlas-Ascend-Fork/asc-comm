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

## Single-Channel BatchHandle Workflow

BatchHandle interfaces currently support only the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950 and do not depend on `Init`.

1. Prepare a UB buffer and create a batch handle from a `ChannelHandle`. Use `auto` to receive the return value.

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

## Shared-Jetty BatchHandle Workflow

When using an HCCL communicator, call the communicator overload of `MakeMultiChannelHandle` on the Host to create the shared-Jetty channels and `MultiChannelHandle`:

```cpp
#include "hcomm/hcomm_host.h"

MultiChannelHandle multiChannel = 0U;
HcclResult ret = MakeMultiChannelHandle(
    comm, sharedQueueTag, channelDescs, channelNum, &multiChannel);
// The communicator manages and automatically releases the shared channels and Device context.
```

Without an HCCL communicator, pass an Hcomm Endpoint and `HcommChannelDesc` array to `MakeMultiChannelHandle`. These resources are not communicator-managed, so destroy the multi-channel handle before destroying the Endpoint:

```cpp
HcommResult ret = MakeMultiChannelHandle(endpointHandle, channelDescs, channelNum, &multiChannel);
// Launch and synchronize the Kernel before destroying the handle.
ret = DestroyMultiChannelHandle(multiChannel);
ret = HcommEndpointDestroy(endpointHandle);
```

On the AICore, create a multi-channel batch handle and call `GetHandleRef` to obtain the common inner execution
handle. Use the inner handle to prepare WQEs and the outer multi-channel batch handle for `BatchCommit` and `Drain`:

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
auto multiBatchHandle = hcomm.MakeBatchHandle(
    static_cast<AscendC::MultiChannelHandle>(multiChannel), batchBuffer, batchBufferLen);
for (uint32_t channelIndex = 0U; channelIndex < channelNum; ++channelIndex) {
    auto& peerBatchHandle = hcomm.GetHandleRef(
        multiBatchHandle, channelIndex, remoteBuffers[channelIndex]);
    ret = hcomm.WriteNbi(
        peerBatchHandle, remoteBuffers[channelIndex], localBuffer, dataLen);
}
ret = hcomm.BatchCommit(multiBatchHandle);
ret = hcomm.Drain(multiBatchHandle);
```

An index in `channelDescs` corresponds to the `channelIndex` passed to `GetHandleRef`. `remoteAddr` selects and caches
a token from that logical channel's remote MR table; when null, it selects the first remote MR. Use the returned inner
reference for batch read and write operations, and use the outer multi-channel batch handle for `BatchCommit` and
`Drain`.

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
| `COMM_PROTOCOL_UBC_CTP` | Supports ordinary read, write, write-with-notify, atomic, commit, and drain interfaces. AIV on Ascend 950 also supports `Lock`, `Unlock`, batch read, batch write, batch write-with-notify, `BatchCommit`, and batch `Drain`. |

## Sharing a Channel Across AI Cores

On Ascend 950, when multiple AIVs share the same `COMM_PROTOCOL_UBC_CTP` channel, use [Lock](../api/aicore/hcomm/Lock.md) and [Unlock](../api/aicore/hcomm/Unlock.md) to protect accesses that update channel state. Each AI Core may use an independent Hcomm object and UB temporary workspace while passing the same `ChannelHandle`.

```cpp
int32_t lockRet = hcomm.Lock(channel);
if (lockRet == 0) {
    int32_t opRet = hcomm.WriteNbi(channel, dst, src, len);

    // A successfully acquired lock must be released even if the communication API fails.
    int32_t unlockRet = hcomm.Unlock(channel);
    // Handle opRet and unlockRet separately.
}
```

The lock is not reentrant. Every successful `Lock` must be paired with `Unlock` on the same AI Core, including on error paths. These interfaces synchronize only AI Cores sharing the same channel on the same device.

## Notes

- Ordinary interfaces require a temporary workspace allocated through `Init`. The current minimum size is 512 bytes. BatchHandle interfaces do not require `Init`.
- When `Init` uses `__ubuf__ uint8_t*`, the implementation aligns the start address of the temporary workspace to 32 bytes. With `LocalTensor`, the caller must ensure sufficient tensor capacity.
- The UB buffer passed to `MakeBatchHandle` must start at a 32-byte aligned address. The caller manages its capacity and lifetime.
- The caller is responsible for initializing and maintaining the channel entity referenced by `ChannelHandle`.
- A BatchHandle caches SQ/CQ contexts and queue cursors at creation time. The caller must exclusively own the corresponding single channel or shared Jetty while using it. Do not mix ordinary interfaces or use multiple BatchHandles concurrently.
- A single transfer through a batch interface must not exceed `UINT32_MAX` bytes.
- The ordinary `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` interfaces are only available for the `COMM_PROTOCOL_UBC_CTP` path.
- Supported data types for atomic operations are limited to `int32_t`, `uint32_t`, `int64_t`, and `uint64_t`.

## Sample

Refer to [hcomm_write_read_nbi](../../../examples/hcomm_write_read_nbi/README_en.md) for the ordinary AIV Kernel-side API workflow and Host-side communication resource creation. Refer to [hcomm_batch_write](../../../examples/hcomm_batch_write/README_en.md) for the shared-Jetty batch write workflow.

The sample executes ordinary `WriteNbi` and `ReadNbi` symmetrically in a two-card scenario:

```cpp
hcomm.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);
hcomm.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);
hcomm.Drain(channel);
```

Both samples require Ascend 950PR/Ascend 950DT and at least two NPUs for runtime verification. Single-NPU environments only support compilation checks.
