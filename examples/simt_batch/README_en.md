# Hcomm SIMT Batch API example

[中文](./README.md)

This example supports two or eight devices. Rank 0 creates root info with `HcclGetRootInfo`, and every rank joins
one HCCL communicator with `HcclCommInitRootInfo`, so no rank table is required. Adjacent ranks form independent
pairs: even ranks 0/2/4/6 launch the SIMT kernel, and odd ranks 1/3/5/7 validate their peer's remote results.

## Runnable minimal example

`basic_op_kernel.cpp` contains the complete minimal path for a first-time batch user: allocate shared-context UB,
create a BatchHandle, execute two `WriteNbi` plus `BatchCommit` pairs, and finally wait with `Drain`.
It has no template dispatch, mixed operations, or status matrix and runs directly on two or eight devices:

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh
```

The Kernel-side call sequence is:

```cpp
constexpr uint32_t kWorkspaceBytes = AscendC::simt::HcommSimtBatchBufferBytes(1U);
alignas(128) __ubuf__ uint64_t batchBuffer[kWorkspaceBytes / sizeof(uint64_t)];

AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0) != 0) {
    return;
}
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), kWorkspaceBytes, remote);
if (batch.context == nullptr ||
    hcomm.WriteNbi(batch, remote, local, 64U) != 0 ||
    hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.WriteNbi(batch, remote + 64U, local + 64U, 64U) != 0 ||
    hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.Drain(batch) != 0) {
    return;
}
```

The caller-owned UB stores one copy of stable queue addresses and remote Jetty fields; its size no longer grows
with the lane count. A BatchHandle binds the remote registration selected by `remote`, and subsequent data and
Notify addresses must remain in it. This is a caller contract and is not rechecked in the hot path. One lane
prepares one request, so Group Batch size equals its lane count, and all operations in a batch must use the same
WQEBB count.

## Full regression design

The suite is centered on the normal explicit-batch call shape instead of using one mixed batch as a proxy for
all APIs:

| Scenario | Batch shape | Main assertion |
| --- | --- | --- |
| Single-lane Write/Read/Notify/WriteValue/FAA/CAS | One Append and Commit per item, then one Drain | Single-item boundary and consecutive publication |
| Group Write/Read/Notify/WriteValue/FAA/CAS | N lanes and one item per lane | Parallel SQ writes and one publication for same-BB operations |
| RMA data | 64 bytes per item and a distinct pattern for all 16 items | Lengths, addresses, and WQEs do not alias |
| FAA | Eight targets updated twice, with 16 separate fetch destinations | No operation is lost/duplicated; every old value is checked |
| CAS | Eight targets compared twice, with 16 separate fetch destinations | First CAS succeeds, second compare fails, and every old value is checked |
| APIs sequential | One single-item batch from each API | Reuse one handle across different APIs |

The suite checks the final device status for every scenario. It validates local Read/FAA/CAS results on every even rank and all
remote data on its odd-rank peer. Any device status or data mismatch makes the corresponding rank fail; successful kernel
launch alone cannot produce an overall PASS.

The existing per-API SIMT examples own immediate-path correctness. The performance suite compares Immediate and
single-lane single-item Batch and Group Batch per API with identical parameters. Group Batch is the primary
batched-performance path.

`MakeBatchHandle` has lane 0 reserve the whole SQ range. During Append, every lane merges its request with shared
Jetty fields directly into its final SQ slot. `BatchCommit` waits for all lanes, has the tail lane publish the batch
through DWQE, and reserves the SQ range for the next batch.

## Pass criteria

- All device calls return zero.
- Every even rank validates Read data and every FAA/CAS old value.
- Every odd rank validates Write/Notify/WriteValue data, Notify signals, and final FAA/CAS values.
- All two or eight ranks and the final `SUITE` line report PASS; otherwise the script returns non-zero.

## Prerequisites

- A CANN/ASC environment supporting `dav-3510` and a two- or eight-device UBC CTP runtime environment.
- An asc-comm development package containing this branch's SIMT batch API and implementation.

```bash
source /usr/local/Ascend/cann/set_env.sh
```

## One-command build and run

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh                 # two-device minimal path (default)
bash run_basic.sh --devices 8     # eight-device minimal path
bash run.sh --devices 2           # full regression (default)
bash run.sh matrix --devices 8    # eight-device matrix
```

`--devices` accepts only 2 or 8 and defaults to 2. Eight-device mode concurrently validates pairs 0→1, 2→3,
4→5, and 6→7. `run.sh` runs the full six-API regression and can also be invoked explicitly as `bash run.sh full`. Set
`BUILD_JOBS=8 bash build.sh` to control build parallelism. The run scripts create and remove temporary
root-info/synchronization files and print the current `CodeVersion` first. Key output lines are:

```text
[rank 0] simt_batch PASS
[rank 1] simt_batch PASS
SUITE | Name=simt_batch | CodeVersion=... | Devices=8 | Pairs=4 | Status=PASS
```
