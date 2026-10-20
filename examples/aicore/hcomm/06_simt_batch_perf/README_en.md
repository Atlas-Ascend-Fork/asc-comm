# Hcomm SIMT Batch Submission Modes Validation and Issue Timing Sample

## Overview

This sample demonstrates how to validate six URMA APIs (`WriteNbi`, `ReadNbi`, `WriteWithNotifyNbi`,
`WriteValueNbi`, `AtomicFAA`, and `AtomicCAS`) and measure the device-side WQE issue timing of different
submission modes in an Ascend C SIMT Kernel, based on the batch submission APIs of `AscendC::simt::Hcomm`.
The sample adopts a rank-pairing topology (one pair for 2 NPUs; four pairs of 0→1, 2→3, 4→5, 6→7 for 8 NPUs),
where each pair establishes its own link, measures independently, and outputs its own result.

The sample covers three submission modes:

- **Immediate**: individual requests posted directly from one lane.
- **ExplicitBatch**: one lane executes `Append → BatchCommit` with a fixed batch size of 1.
- **GroupBatch**: multiple lanes each build one request, one `BatchCommit` per batch; the batch size equals the lane count.

The primary metric `IssueTicksPerRequest` is the device `clock()` delta over the timed submission interval
divided by the request count; lower is better.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
simt_batch_perf
├── CMakeLists.txt            // CMake build file
├── main.cpp                  // Host-side communication domain setup, memory registration, case scheduling, and validation
├── op_kernel.cpp             // Hcomm API calls and issue timing in the SIMT Kernel
├── simt_batch_perf_common.h  // Definitions shared by Host and Kernel
├── README.md                 // Chinese sample documentation
└── README_en.md              // English sample documentation
```

## Sample Description

### Functionality Description

Test semantics of the six APIs (SIMT path, WQEs issued directly inside the Kernel):

| API | Data Flow | Semantic Description |
| --- | --- | --- |
| `WriteNbi` | Local GM → Remote GM | Writes local Global Memory data to a specified address on the remote NPU. |
| `ReadNbi` | Remote GM → Local GM | Reads data from a specified address on the remote NPU into local Global Memory. |
| `WriteWithNotifyNbi` | Local GM → Remote GM | Writes the payload and then a remote notify word. |
| `WriteValueNbi` | Local WQE → Remote GM | Carries an 8-byte value inline in the WQE and writes it to the remote side without reading the local buffer. |
| `AtomicFAA` | Remote GM ↔ Local GM | Adds to a remote 8-byte word and writes the fetched value back locally. |
| `AtomicCAS` | Remote GM ↔ Local GM | Compares and swaps a remote 8-byte word and writes the fetched value back locally. |

Three submission modes:

| Submission Mode | Description |
| --- | --- |
| Immediate | Each request goes through one individual submission; APIs such as `WriteNbi(batch)` post one WQE at a time. |
| ExplicitBatch | One lane enqueues with `Append` first and publishes the batch with `BatchCommit`; the batch size is fixed at 1. |
| GroupBatch | Every lane in the group builds one request, and the last publisher lane publishes the whole batch; the batch size equals the lane count. |

Data validation policy:

- Each case checks both the device-returned status (`Drain` status and completion count) and the landed data.
- **Read** checks the remote data landed locally; **Write/Notify** check the receiver payload and notify value;
  **FAA/CAS** check both the fetched value (sender) and the remote accumulate/swap result (receiver).
- Timed and warmup requests rotate over 64 slots to avoid all landing on one cache line; validation is
  performed per slot instead of per operation.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Point-to-point paired communication (one pair for 2 NPUs; NPUs paired two by two for 8 NPUs, pairs run concurrently) |
| Invocation Method | APIs of `AscendC::simt::Hcomm` issued within a SIMT Kernel |
| Submission Modes | `compare` (default, Immediate + GroupBatch), `immediate`, `batch`, `group` |
| Process Launch | Executable directly launches, internally forks multiple ranks |
| Supported Rank Count | 2 (one pair) or 8 (four pairs), not exceeding the number of available NPUs |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   ranks exchange root info through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`)
   and create the communication domain. The parent process launches one round of rank processes per case.
2. Call `HcclCommMemReg` to register the local send/recv buffers with the communication domain; memory information is
   automatically exchanged with the peer during channel creation.
3. Use `HcclRankGraphGetLayers` and `HcclRankGraphGetLinks` to obtain the link endpoint to the peer, call
   `HcclChannelAcquire` to create the P2P channel (engine `COMM_ENGINE_AIV`, protocol `COMM_PROTOCOL_UBC_CTP`),
   and call `HcclChannelGetRemoteMems` to resolve the registration index of the remote recv buffer.
4. The Host pre-initializes the send/recv data (atomics get a preset remote initial value), and the sender launches
   the Kernel after a barrier.
5. The Kernel side executes `Init()` to allocate the UB workspace, then issues warmup and timed requests according
   to the submission mode: consecutive requests rotate over 64 slots, and `Drain()` (`Drain(channel)` for Immediate,
   `Drain(batch)` for Batch modes) waits for completion.
6. The sender reads back the timing and completion status, and additionally validates local data for Read/Atomic
   APIs; the receiver validates the landed Write/Notify data and remote atomic results.
7. Validation results of both ends are gathered over the control channel by allgather; the sender prints the RESULT
   line of the pair, and the parent process prints the aggregate result.

```cpp
// Minimal single-lane Batch call sequence (ExplicitBatch, batch size 1):
constexpr uint32_t bytes = 256U; // 128B shared context + 128B publisher DWQE image
alignas(128) __ubuf__ uint64_t workspace[bytes / sizeof(uint64_t)];
AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0U) != 0) return;
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remote, local, 64U) != 0) return;
if (hcomm.BatchCommit(batch) != 0) return;
if (hcomm.Drain(batch) != 0) return;
```

Group calls use the same Hcomm object without storing a Group, and pass the group explicitly at the end of each
batch operation:

```cpp
// The calls below are joined by all lanes in the group; each lane builds its own request.
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U, group);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remoteForLane, localForLane, 64U, group) != 0) return;
if (hcomm.BatchCommit(batch, group) != 0) return;
// After all posting lanes return, one lane calls Drain; Drain takes no group.
```

## Compilation and Execution

Follow the steps below in the root directory of this sample to compile and run it. This sample only supports NPU execution mode.

- Configure environment variables

  The sample is compiled against the asc-comm headers installed under the CANN directory. If they are not installed,
  first generate and install the development run package from the repository root; see
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
  cd .. && ./build/simt_batch_perf tcp://127.0.0.1:29626 2 write
  ```

  Common usage examples:

  ```bash
  # Default compare mode: Immediate vs GroupBatch (2/4/8/16/32/128 lanes).
  ./build/simt_batch_perf tcp://127.0.0.1:29626 2 write
  # Single-lane ExplicitBatch mode.
  ./build/simt_batch_perf tcp://127.0.0.1:29626 2 write --mode batch
  # Specify the API and GroupBatch lanes.
  ./build/simt_batch_perf tcp://127.0.0.1:29626 2 cas --mode group --group-lanes 16,32,128
  # Run all six APIs, four pairs on 8 NPUs.
  ./build/simt_batch_perf tcp://127.0.0.1:29626 8 all
  ```

- Launch parameter description

  ```bash
  ./build/simt_batch_perf <ip:port> <nranks> <write|read|notify|write_value|faa|cas|all> [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of ranks to launch; 2 (one pair) or 8 (four pairs), not exceeding the number of available NPUs |
  | `<api>` | None | Communication API to test; `all` runs the six APIs in turn |
  | `--mode` | `compare` | `compare` (Immediate + GroupBatch), `immediate`, `batch`, or `group` |
  | `--iterations` | `1024` | Timed request count; must be divisible by the lane count in GroupBatch mode |
  | `--warmup` | `96` | Warmup request count; rounded up to a whole batch in GroupBatch mode |
  | `--payload-bytes` | `64` | Payload bytes for `write`/`read`/`notify`, minimum 2; `write_value`, `faa`, and `cas` always use 8 bytes and ignore this option |
  | `--group-lanes` | `2,4,8,16,32,128` | Comma-separated GroupBatch lane counts, i.e. requests per batch |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU architecture: `dav-3510` corresponds to Ascend 950PR&950DT products |
  | `SIMT_BATCH_ENABLE_LARGE_GROUP` | `OFF` (default) / `ON` | Whether to compile the 1024-lane GroupBatch path; lane counts above 128 require rebuilding with `-DSIMT_BATCH_ENABLE_LARGE_GROUP=ON` |

- Expected output

  After a successful multi-card run, the terminal outputs the following — one line per rank pair per case, followed
  by the aggregate result:

  ```text
  RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
  RESULT | Example=simt_batch_perf | Status=PASS
  ```

  The primary metric `IssueTicksPerRequest` = the device `clock()` delta over the timed submission interval / the
  request count; lower is better. It includes construction, submission, and submission-path synchronization, and
  excludes the subsequent `Drain` wait; the unit is raw ticks, not converted to ns. `BatchSize` is the request count
  per batch; `Jettys` and `ConcurrentJettys` are the configured count and the actual maximum parallel count
  respectively, both 1 in this single-Jetty sample; `Pair` is the rank pair. Only compare PASS rows, and do not
  compute a speedup directly against SIMD data whose clock units are not aligned.

## Notes

- Running the sample requires at least 2 NPUs; a single-card environment only supports compilation verification.
- A BatchHandle is bound to the remote registration containing `remote`; subsequent target addresses must lie within
  the same region. The final `1U` of `MakeBatchHandle` specifies the per-item BB count for an ordinary Write.
- Group calls use the same Hcomm object without storing a Group, and pass the group explicitly at the end of each
  batch operation; the same Group (identical members, ranks and size) and the same UB workspace must be used from
  creation to commit of a handle. The no-group overloads are for single-lane handles; a size-1 Group uses single-lane
  dispatch. Write/Read/Notify/WriteValue/FAA/CAS all accept the group as the final argument, with the explicit config
  template parameters before the group.
- `IssueTicksPerRequest` measures the average submission cost, not the end-to-end latency of a single request: it
  includes construction/publication and submission-path synchronization, excludes the subsequent `Drain` wait, and
  does not count warmup or host launch gaps.
- `--payload-bytes` has a minimum of 2: a committed `WriteNbi` splits its payload across two SGEs, and a 1-byte
  payload would give the first SGE a zero length, which URMA does not accept.
