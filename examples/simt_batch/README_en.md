# Hcomm SIMT Batch API example

[中文](./README.md)

This example follows the repository's two-rank SIMT structure. Rank 0 creates root info with `HcclGetRootInfo`,
and both ranks create the HCCL communicator with `HcclCommInitRootInfo`, so no rank table is required. The host
registers memory and acquires a UBC CTP channel; rank 0 launches a single-lane SIMT kernel and rank 1 validates
the remote results.

## Runnable minimal example

`basic_op_kernel.cpp` contains the complete minimal path for a first-time batch user: allocate UB for two items,
create a BatchHandle, append two `WriteNbi` operations, publish them with one `BatchCommit`, and wait with `Drain`.
It has no template dispatch, mixed operations, or status matrix and can be built and run directly on two ranks:

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh
```

The Kernel-side call sequence is:

```cpp
constexpr uint32_t kItemCount = 2U;
constexpr uint32_t kWorkspaceBytes = AscendC::simt::HcommSimtBatchBufferBytes(kItemCount);
alignas(128) __ubuf__ uint64_t batchBuffer[kWorkspaceBytes / sizeof(uint64_t)];

AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0) != 0) {
    return;
}
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), kWorkspaceBytes, remote);
if (!batch.valid ||
    hcomm.WriteNbi(batch, remote, local, 64U) != 0 ||
    hcomm.WriteNbi(batch, remote + 64U, local + 64U, 64U) != 0 ||
    hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.Drain(batch) != 0) {
    return;
}
```

The caller owns the UB and chooses its capacity. `HcommSimtBatchBufferBytes` exposes the current basic-format
sizing rule so user code does not depend on an internal WQE constant. A BatchHandle binds one remote
registration selected by `remote`; subsequent data and Notify ranges must stay inside it. The implementation
performs a lightweight check against the base and size cached in the handle.

## Full regression design

The suite is centered on the normal explicit-batch call shape instead of using one mixed batch as a proxy for
all APIs:

| Scenario | Batch shape | Main assertion |
| --- | --- | --- |
| Homogeneous Write/Read/Notify/WriteValue/FAA/CAS | Two batches of eight items per API, then one Drain | Every API is the publishing tail; consecutive commits complete |
| RMA data | 64 bytes per item and a distinct pattern for all 16 items | Lengths, addresses, and WQEs do not alias |
| FAA | Eight targets updated twice, with 16 separate fetch destinations | No operation is lost/duplicated; every old value is checked |
| CAS | Eight targets compared twice, with 16 separate fetch destinations | First CAS succeeds, second compare fails, and every old value is checked |
| Mixed partial batch | One item from every API, six items total | Different WQE types coexist; CAS is the tail |
| Single-item batch | One `WriteValueNbi` | The `batch-size=1` boundary |

The suite first records handle validity, verifies that it selected the registration returned by `RemoteBufferAddr`,
and records logical capacity and physical SQ depth. Each scenario records `Append`, `BatchCommit`, `Drain`, and
`BatchCommitCount`. The suite validates local
Read/FAA/CAS results on rank 0 and all remote data on rank 1. Any device status, publish-count, or data mismatch
makes the corresponding rank fail; successful kernel submission alone cannot produce an overall PASS.

The existing per-API SIMT examples own immediate-path correctness. The performance suite compares Immediate and
ExplicitBatch per API with identical parameters. Mixed and single-item cases are supplemental; homogeneous
multi-item and multi-commit submission is the primary gate.

The basic implementation uses 128 bytes of UB per item to reuse the validated immediate WQE layout. The example
uses the public `HcommSimtBatchBufferBytes` helper rather than an internal magic number. Per-operator WQEBB
compaction is a later optimization and is not part of the current API semantics.

## Pass criteria

- The handle is valid and bound to the selected remote registration, all device steps return zero, and
  BatchCommit call counts match the design.
- Rank 0 validates Read data and every FAA/CAS old value.
- Rank 1 validates Write/Notify/WriteValue data, Notify signals, and final FAA/CAS values.
- Both ranks and the final `SUITE` line report PASS; otherwise the script returns non-zero.

## Prerequisites

- A CANN/ASC environment supporting `dav-3510` and a two-rank UBC CTP runtime environment.
- An asc-comm development package containing this branch's SIMT batch API and implementation.

```bash
source /usr/local/Ascend/cann/set_env.sh
```

## One-command build and run

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh  # minimal usage path
bash run.sh
```

`run.sh` runs the full six-API regression and can also be invoked explicitly as `bash run.sh full`. Set
`BUILD_JOBS=8 bash build.sh` to control build parallelism. The run scripts create and remove temporary
root-info/synchronization files and print the current `CodeVersion` first. Key output lines are:

```text
STATUS | Case=WriteNbi.homogeneous | Step=BatchCommit | Code=0 | PASS
CHECK | API=AtomicFAA.fetch | ...
[rank 0] simt_batch PASS
[rank 1] simt_batch PASS
SUITE | Name=simt_batch | CodeVersion=... | Status=PASS
```
