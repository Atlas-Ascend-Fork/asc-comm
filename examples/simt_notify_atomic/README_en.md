# SIMT Notify/FAA/CAS Sample

This sample validates the SIMT URMA `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` interfaces. Rank 0 submits
communication operations and verifies local fetch values and `Drain` status, while rank 1 verifies remote results.

## Test Modes

- `notify`: Submits one Notify operation and calls `Drain`.
- `faa`: Submits one FAA operation and verifies the remote sum and locally fetched old value.
- `cas`: Submits one CAS operation and verifies the remote swap result and locally fetched old value.
- `single`: Serially submits Notify, FAA, and CAS, calling `Drain` after each operation.
- `batch_last`: Defers Notify and FAA with `commit=false`, then publishes the batch with a `commit=true` CAS.
- `multi_lane`: Three lanes defer Notify, FAA, and CAS; after synchronization, lane 0 appends a Notify operation and
  publishes the batch.
- `notify_immediate_repeat`: A single lane submits 100 `WriteWithNotifyNbi<true>` WQEs, immediately publishing every
  WQE and calling `Drain` once after the full series.
- `warp_immediate`: Three lanes each perform one immediate submission at the same call site, covering the single-warp
  cooperative reservation and publication path.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_notify_atomic
bash build.sh
```

`build.sh` removes the existing `build` directory before configuring and rebuilding the sample, preventing stale
headers or objects from being reused.

## Run

As with the existing `simt_write` sample, an HCCL rank table must be provided:

```bash
bash run.sh 2 /path/to/rank_table.json notify
bash run.sh 2 /path/to/rank_table.json faa
bash run.sh 2 /path/to/rank_table.json cas
bash run.sh 2 /path/to/rank_table.json single
bash run.sh 2 /path/to/rank_table.json batch_last
bash run.sh 2 /path/to/rank_table.json multi_lane
bash run.sh 2 /path/to/rank_table.json notify_immediate_repeat
bash run.sh 2 /path/to/rank_table.json warp_immediate
```

In `warp_immediate`, three lanes each execute one `commit=true` Notify, FAA, or CAS call at the same call site. This
directly covers the library path that combines single-warp SQ reservation, ordinary SQ writes by non-publishing lanes,
and final DWQE publication by the publishing lane.

Expected output for each mode:

```text
[rank 0] simt_notify_atomic <mode> PASS
[rank 1] simt_notify_atomic <mode> PASS
```

## Buffer Index Constraints

`LocalBufferAddr` and `RemoteBufferAddr` take indices into the channel buffer tables rather than indices into the
`memHandles` array passed to `HcclChannelAcquire`. Local table index 0 is reserved for the CCL buffer, so the first
explicitly registered `sendBuf` uses index 1 in this sample. The remote `recvBuf` index is found by tag through
`HcclChannelGetRemoteMems`.

This sample requires at least two Ascend 950PR/950DT devices on the same host and a CANN version that supports the SIMT
URMA interfaces. Ranks synchronize through local temporary files.
