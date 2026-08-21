# SIMT Notify/FAA/CAS Sample

This sample validates the SIMT URMA `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` interfaces. Rank 0 submits
communication operations and verifies local fetch values and `Drain` status, while rank 1 verifies remote results.

## Test Modes

- `notify`: Submits one Notify operation and calls `Drain`.
- `faa`: Submits one FAA operation and verifies the remote sum and locally fetched old value.
- `cas`: Submits one CAS operation and verifies the remote swap result and locally fetched old value.
- `single` (default): Serially submits Notify, FAA, and CAS, calling `Drain` after each operation.
- `batch_last`: Defers Notify and FAA with `commit=false`, then publishes the batch with a `commit=true` CAS.
- `notify_immediate_repeat`: A single lane submits 100 `WriteWithNotifyNbi<true>` WQEs, immediately publishing every
  WQE and calling `Drain` once after the full series. Covers repeated immediate submission.

Every mode posts from a single lane. An Hcomm object is lane-private state, and one channel must be driven by one
lane: a deferred WQE stays in the send queue until a later `commit=true` post publishes a producer index covering it,
and that index cannot distinguish which lane wrote which basic block.

## Preparing the Development Verification Environment

The sample builds against the asc-comm headers installed into the CANN directory. Generate and install the
development verification run package from the repository root first:

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh --pkg
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --full
```

Pass `--install-path=/path/to/cann` to select a different CANN directory. See
[`docs/en/guide/build_and_test.md`](../../docs/en/guide/build_and_test.md) for the full option list.

## Build

```bash
cd examples/simt_notify_atomic
bash build.sh
```

`build.sh` removes the existing `build` directory before configuring and rebuilding the sample, preventing stale
headers or objects from being reused.

## Run

Running matches the existing `simt_write` sample and requires an HCCL rank table:

```bash
bash run.sh 2 /path/to/rank_table.json notify
bash run.sh 2 /path/to/rank_table.json faa
bash run.sh 2 /path/to/rank_table.json cas
bash run.sh 2 /path/to/rank_table.json single
bash run.sh 2 /path/to/rank_table.json batch_last
bash run.sh 2 /path/to/rank_table.json notify_immediate_repeat
```

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
