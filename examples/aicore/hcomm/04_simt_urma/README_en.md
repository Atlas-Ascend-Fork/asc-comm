# Hcomm SIMT URMA Functional Sample

Validates every SIMT URMA point-to-point interface in `hcomm/hcomm_simt.h`: `WriteNbi`,
`WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`, under both immediate and
deferred publication.

The topology is a ring: every rank posts into the memory registered by
`next = (rank + 1) % nranks` and verifies what `prev = (rank - 1 + nranks) % nranks` wrote. So with
three or more cards every rank both sends and receives, rather than only the first two working
while the rest idle.

Each rank is both sender and receiver, and both sides are checked: `sendBuf` holds the values this
rank fetched back, `recvBuf` holds what `prev` wrote. Remote writes only ever land in `recvBuf` —
the peer index is looked up by `kRecvBufTag`, and although `sendBuf` is registered on the channel it
is never a remote target — so each `recvBuf` on the ring has exactly one writer and both checks have
deterministic expected values.

What a rank sends does not depend on its rank, so the receiver's expected-value table does not need
to distinguish where the data came from.

Every scenario is driven by a single SIMT thread. One channel must be driven by one lane: a deferred
WQE stays in the send queue until a later `commit=true` post publishes a producer index covering the
whole batch, and that index cannot distinguish which lane wrote which basic block. All tasks in one
batch must be posted by the same lane.

## Test Modes

### Write family

`WriteNbi` takes its data from a local buffer, and the WQE carries an SGE pointing at that buffer.
Slot `i` carries the value `i + 1`, so the buffer reads back as 1, 2, ... 32; any wrong value points
straight at the slot it should have come from.

| `mode` | Kernel | Publication |
|---|---|---|
| `write_single` | `SimtWriteSingle` | 32 independent `WriteNbi<commit=true>` calls |
| `write_batch_last` | `SimtWriteBatchLast` | 31 serial `WriteNbi<false>`, then one `WriteNbi<true>` |

`WriteValueNbi` carries the value inline in the WQE with no SGE pointing at local memory, so these
two modes never read the send buffer at all (it stays registered only to keep the buffer tables
symmetric across ranks):

| `mode` | Kernel | Publication |
|---|---|---|
| `write_value_single` | `SimtWriteValueSingle` | 32 independent `WriteValueNbi<commit=true>` calls |
| `write_value_batch_last` | `SimtWriteValueBatchLast` | 31 serial `WriteValueNbi<false>`, then one `WriteValueNbi<true>` |

All four modes cover the full 32 slots and the receiver-side check is identical for each. The
`*_single` modes publish every post with its own doorbell; the `*_batch_last` modes rely on the
producer index carried by the final immediate post to sweep up the 31 deferred WQEs before it.

`WriteValueNbi` requires `config` to have inline enabled (`URMA_INLINE_CFG` is the default) and
`sizeof(T)` to fit the WQE inline payload area; both are checked at compile time.

### Notify/Atomic family

| `mode` | Description |
|---|---|
| `notify` | Submits one Notify operation and calls `Drain` |
| `faa` | Submits one FAA operation, verifying the remote sum and the locally fetched old value |
| `cas` | Submits one CAS operation, verifying the remote swap and the locally fetched old value |
| `single` (default) | Submits Notify, FAA, and CAS serially, calling `Drain` after each |
| `batch_last` | Defers Notify and FAA with `commit=false`, then publishes the batch with a `commit=true` CAS |
| `notify_immediate_repeat` | One lane submits `kNotifyImmediateRepeatCount` `WriteWithNotifyNbi<true>` posts back to back, ringing the doorbell for each WQE, then calls `Drain` once after the whole burst; covers repeated immediate submission |

`kNotifyImmediateRepeatCount` is defined in [`simt_urma_common.h`](./simt_urma_common.h), currently 100.

The two families use disjoint slot layouts: each was designed around what its own operations need to
observe, and keeping them apart makes every mode's expected-value table easier to read. The buffer is
sized to whichever layout is larger.

SIMT has no `Commit` interface, so a deferred task can only be carried out by a later `commit=true`
post. Every mode ends with `Drain` to make sure the CQ has been consumed before the kernel returns.

## Build and Run

Perform the following steps in the sample root directory. This sample supports NPU run mode only.

- Set Environment Variables

  The sample builds against the asc-comm headers installed into the CANN directory. If not yet
  installed, generate and install the development verification run package from the repository root
  (see [`docs/en/guide/build_and_test.md`](../../../../docs/en/guide/build_and_test.md)):

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --full
  ```

  Then load the CANN environment:

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN installation directory. If not specified, the default is
  > `/usr/local/Ascend`.

- Run the Sample

  The executable forks all rank processes internally:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/simt_urma tcp://127.0.0.1:29620 <nranks> [mode]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | Required | Control channel address that rank 0 listens on (`tcp://ip:port`); use different ports for parallel instances |
  | `<nranks>` | Required | Number of ranks; must be >= 2 |
  | `[mode]` | `single` | Test mode; see Test Modes above; unrecognized modes are rejected outright |

- Build Options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture for Ascend 950PR/Ascend 950DT |

- Expected Output

  ```text
  [rank 0] simt_urma write_single | sent to rank 1, received from rank 1 | PASS
  [rank 1] simt_urma write_single | sent to rank 0, received from rank 0 | PASS
  RESULT | Example=simt_urma Mode=write_single | Status=PASS
  ```

  The sample succeeds when all ranks print `PASS` and the final status is `PASS`.

## Buffer Index Constraints

The kernel resolves buffer base addresses through `AscendC::simt::LocalBufferAddr` and
`RemoteBufferAddr`, both of which take an index into the channel's buffer table rather than an index
into the `memHandles` array passed to `HcclChannelAcquire`. Local table index 0 is reserved for the
CCL buffer, so the first explicitly registered `sendBuf` in this sample uses index 1. The remote
`recvBuf` index is looked up by tag through `HcclChannelGetRemoteMems`, because the remote table may
hold entries this sample did not register.

Passing the `memHandles` position directly resolves to the CCL buffer instead: the WQE, the remote
address, and the token are all valid, so the NIC completes it and returns a clean CQE, but the
receiver observes the CCL buffer's contents rather than the payload.

## Host-side Synchronization

Two host barriers make the verification trustworthy. They use the `RankSyncContext` barrier
primitives over the TCP control channel.

| Barrier | Purpose |
|---|---|
| `ready` | Every rank has finished memory registration and channel creation before any writing starts |
| `sent` | Every rank has passed `aclrtSynchronizeStream`, so it may now read its own recvBuf |

The `sent` barrier is what makes the result trustworthy. Without it a rank runs straight from `ready`
to `aclrtMemcpy` with no ordering constraint against the kernel of the rank writing into it, reads
the zeroed buffer, and reports every slot as never landed even though all CQEs returned cleanly.

## Runtime Constraints

- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required to run; single-NPU environments only support compilation verification.
- The executable captures `SIGINT`/`SIGTERM` and terminates all rank processes, so Ctrl+C does not
  leave processes blocked on a host barrier still holding devices.
- The build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library.
