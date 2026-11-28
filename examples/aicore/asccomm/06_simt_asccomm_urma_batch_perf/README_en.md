# CommRW SIMT Batch Submission Modes Validation and Issue Timing Sample

## Overview

This sample demonstrates how to validate the functional correctness of six URMA APIs (`write_nbi`, `read_nbi`,
`write_with_notify_nbi`, `write_value_nbi`, `atomic_faa_nbi`, and `atomic_cas_nbi`) and measure the device-side WQE issue timing
of different submission modes in an Ascend C SIMT Kernel, based on the batch submission APIs of
`asc::simt::asccomm`.
The sample adopts a rank-pairing topology (2 NPUs directly connected as one pair; 8 NPUs paired as 0→1, 2→3,
4→5, 6→7 into four pairs), where each pair establishes its own link, measures independently, and outputs its
own result.

The sample covers two submission modes:

- **ExplicitBatch**: one lane executes `Append → batch_commit` with a fixed batch size of 1.
- **GroupBatch**: each lane builds one request per round. One `batch_commit` can submit one or more rounds.

Write GroupBatch can use several channels concurrently per rank pair. A single SIMT thread block is
partitioned into cooperative groups. Each group uses its own channel, UB BatchHandle workspace, and
slices of the send and receive buffers.
The launch uses the channel count multiplied by the number of lanes per group. For example, three channels
with 32 lanes per group launch 96 threads and use `__launch_bounds__(128)` for the VF.

The primary metric `IssueTicksPerRequest` is the device `clock()` delta over the timed submission interval
divided by the request count; lower is better.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
06_simt_asccomm_urma_batch_perf
├── CMakeLists.txt            // CMake build file
├── simt_batch_perf.asc       // Host-side communication domain setup, memory registration, case scheduling, and validation
├── op_kernel.cpp             // CommRW API calls and issue timing in the SIMT Kernel
├── simt_batch_perf_common.h  // Definitions shared by Host and Kernel
├── README.md                 // Chinese sample documentation
└── README_en.md              // English sample documentation
```

## Sample Description

### Functionality Description

Test semantics of the six APIs (SIMT path, WQEs issued directly inside the Kernel):

| API | Data Flow | Semantic Description |
| --- | --- | --- |
| `write_nbi` | Local GM → Remote GM | Writes local Global Memory data to a specified address on the remote NPU. |
| `read_nbi` | Remote GM → Local GM | Reads data from a specified address on the remote NPU into local Global Memory. |
| `write_with_notify_nbi` | Local GM → Remote GM | Writes the payload and then a remote notify word. |
| `write_value_nbi` | Local WQE → Remote GM | Carries an 8-byte value inline in the WQE and writes it to the remote side without reading the local buffer. |
| `atomic_faa_nbi` | Remote GM ↔ Local GM | Adds to a remote 8-byte word and writes the fetched value back locally. |
| `atomic_cas_nbi` | Remote GM ↔ Local GM | Compares and swaps a remote 8-byte word and writes the fetched value back locally. |

Two submission modes:

| Submission Mode | Description |
| --- | --- |
| ExplicitBatch | One lane enqueues with `append_one_wqe` first and publishes the batch with `batch_commit`; the batch size is fixed at 1. |
| GroupBatch | Each lane builds one request per round. The group submits all accumulated requests together. |

Data validation policy:

- Each case checks both the device-returned status (`drain` status and completion count) and the landed data.
- **Read** checks the remote data landed locally; **Write/Notify** check the receiver payload and notify value;
  **FAA/CAS** check both the fetched value (sender) and the remote accumulate/swap result (receiver).
- Each phase rotates requests over 64 slots and validates results per slot. GroupBatch uses separate slots for
  warmup and timed requests.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Point-to-point paired communication (one pair for 2 NPUs; NPUs paired two by two for 8 NPUs, pairs run concurrently) |
| Invocation Method | APIs of `asc::simt::asccomm` issued within a SIMT Kernel |
| Submission Modes | `compare` runs both ExplicitBatch and GroupBatch. `batch` runs ExplicitBatch only, and `group` runs GroupBatch only. |
| Process Launch | Executable directly launches, internally forks multiple ranks |
| Supported Rank Count | 2 (one pair) or 8 (four pairs), not exceeding the number of available NPUs |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   ranks exchange root info through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`)
   and create the communication domain. The parent process launches one round of rank processes per case.
2. Call `HcclCommMemReg` to register the two send/recv communication buffers of the local NPU with the
   communication domain; memory information is automatically exchanged with the peer during channel creation.
3. Use `HcclRankGraphGetLayers` and `HcclRankGraphGetLinks` to obtain the link endpoint to the peer, call
   `HcclChannelAcquire` once for all requested P2P channels (engine `COMM_ENGINE_AIV`, protocol `COMM_PROTOCOL_UB_CTP`).
   Resolve the remote receive-buffer registration index for each channel with `HcclChannelGetRemoteMems`
   and copy the channel table to the device.
4. The Host pre-initializes the send/recv data (atomics get a preset remote initial value), and the sender launches
   the Kernel after a barrier.
5. For each channel, the kernel assigns a dedicated 256-byte UB workspace and separate slices of the send and
   receive buffers. It issues warmup and timed requests using the selected submission mode.
   In GroupBatch mode, all groups finish warmup before timed submission starts. Each group appends and commits independently.
   Once all groups finish issuing requests, each group calls `drain(batch)` to wait for completion.
6. The sender reads back the timing and completion status, and additionally validates local data for Read/Atomic
   APIs; the receiver validates the landed Write/Notify data and remote atomic results.
7. Validation results of both ends are gathered over the control channel by allgather; the sender prints the RESULT
   line of the pair, and the parent process prints the aggregate result.

```cpp
// Minimal single-lane Batch call sequence (ExplicitBatch, batch size 1):
constexpr uint32_t bytes = 256U; // 128 B shared context + 128 B publisher DWQE image
alignas(128) __ubuf__ uint64_t workspace[bytes / sizeof(uint64_t)];
asc::simt::asccomm<> asccomm;
if (asccomm.init(nullptr, 0U) != 0) return;
auto batch = asccomm.make_batch_handle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes);
if (batch.context == nullptr) return;
if (asccomm.write_nbi(batch, remote, local, 64U) != 0) return;
if (asccomm.batch_commit(batch) != 0) return;
if (asccomm.drain(batch) != 0) return;
```

GroupBatch calls use the same asccomm object without storing a Group, and pass the group explicitly for each append
round and the final submission:

```cpp
// The calls below are joined by all lanes in the group; each lane builds its own request in each round.
auto batch = asccomm.make_batch_handle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, group);
if (batch.context == nullptr) return;
uint32_t rounds = 0U;
for (; rounds < 2U; ++rounds) {
    const bool first = rounds == 0U;
    if (asccomm.write_nbi(batch, first ? remote_for_lane : remote_for_lane_2,
            first ? local_for_lane : local_for_lane_2, 64U, group) != 0) {
        break;
    }
}
if (rounds == 0U || asccomm.batch_commit(batch, group) != 0) return;
if (group.thread_rank() == 0U) {
    asccomm.drain(batch);
}
```

## Compilation and Execution

Follow the steps below in the root directory of this sample to compile and run it. This sample only supports NPU execution mode.

- Configure environment variables

  The sample is compiled against the asc-comm headers installed under the CANN directory. If they are not installed,
  first generate and install the development verification run package from the repository root; see
  [`docs/en/compilation/build_and_test.md`](../../../../docs/en/compilation/build_and_test.md) for details:

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_9.3.0_linux-<arch>.run --full
  ```

  Then load the CANN environment variables:

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If not specified, it defaults to `/usr/local/Ascend`.

- Run the sample

  Execute the following commands in the sample directory. The executable internally forks all rank processes, so it can be run directly:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write
  ```

  Common usage examples:

  ```bash
  # The single-channel default runs ExplicitBatch and GroupBatch (16, 32, and 128 lanes).
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write
  # Single-lane ExplicitBatch mode.
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --mode batch
  # Four channels per rank pair, with one 32-lane group per channel.
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --channels 4 --group-lanes 32
  # Single-channel GroupBatch with a group size of 2048 lanes.
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --mode group --group-lanes 2048 --iterations 2048
  # Two-channel Write GroupBatch with 1024 lanes per group.
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --channels 2 --group-lanes 1024
  # Specify the API and GroupBatch lanes.
  ./simt_batch_perf tcp://127.0.0.1:29626 2 cas --mode group --group-lanes 16,32,128
  # Run all six APIs; on 8 NPUs, four pairs run concurrently.
  ./simt_batch_perf tcp://127.0.0.1:29626 8 all
  ```

- Launch parameter description

  ```bash
  ./simt_batch_perf <ip:port> <nranks> <write|read|notify|write_value|faa|cas|all> [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of ranks to launch; 2 (one pair) or 8 (four pairs), not exceeding the number of available NPUs |
  | `<api>` | None | Communication API to test; `all` runs the six APIs in turn |
  | `--mode` | `compare` for one channel, `group` for multiple channels | Choose `compare`, `batch`, or `group`. |
  | `--channels` | `1` | Channels per rank pair, from 1 to 32. Multiple channels require `write` in `group` mode. |
  | `--iterations` | `1024` | Timed requests per channel, at least 1. In GroupBatch mode, the count must be divisible by the number of lanes per group. |
  | `--warmup` | `96` | Warmup requests per channel. Use 0 to skip warmup. In GroupBatch mode, the count is rounded up to a full append round. |
  | `--payload-bytes` | `64` | Payload size for `write`, `read`, and `notify`, from 2 bytes to 256 MiB, subject to available device memory. `write_value`, `faa`, and `cas` always transfer 8 bytes. |
  | `--group-lanes` | `16,32,128` for `--channels` values of 1–8, `16,32` for values of 9–32 | Comma-separated group sizes in lanes per channel, from 16 to 2048. In multi-channel mode, each group size must be a power of two and the product of the channel count and group size must not exceed 2048. |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU architecture: `dav-3510` corresponds to Ascend 950PR&950DT products |

- Expected output

  After a successful multi-card run, the terminal outputs the following — one line per rank pair per case, followed
  by the aggregate result:

  ```text
  RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Publishes=1 | Pair=0-1 | Status=PASS
  RESULT | Example=simt_batch_perf | Status=PASS
  ```

  `IssueTicksPerRequest` is the device `clock()` delta from the earliest issue start to the last commit return,
  divided by the total timed request count across the rank pair's channels. The interval covers request construction,
  commit, synchronization, and queue-pressure waits between commits. The unit is raw device ticks. The final
  drain, warmup, and host launch gaps fall outside the issue interval. `Publishes` reports successful timed commits across all channels.
  `BatchSize` is the request count per append round per channel. `Jettys` and `ConcurrentJettys`
  both report channels per rank pair. `Pair` identifies the ranks.

## Notes

- Running the sample requires at least 2 NPUs; a single-card environment only supports compilation verification.
- GroupBatch requires at least 16 consecutive hardware threads in rank order. Group rank 0 must start at hardware warp lane 0 or 16.
- GroupBatch reserves queue space one round at a time. Each channel's SQ and CQ must have sufficient capacity
  for the WQEs and CQEs of one round, respectively.
- A BatchHandle is bound to the remote registrations of its channel; subsequent target addresses must lie within the
  same regions. All requests in a batch must use the same WQE width.
- Group calls use the same asccomm object without storing a Group, and pass the group explicitly for each append round
  and the final submission; the same Group (identical members, ranks and size) and the same UB workspace must be used from
  creation to commit of a handle. The no-group overloads are for single-lane handles. Write/Read/Notify/WriteValue/FAA/CAS all accept the group as the final argument, with the explicit config
  template parameters before the group.
- `--payload-bytes` has a minimum of 2: a `write_nbi` splits its payload across two SGEs, and a 1-byte
  payload would give the first SGE a zero length, which URMA does not accept.
