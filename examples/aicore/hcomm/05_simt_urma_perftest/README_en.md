# Hcomm SIMT URMA Performance Sample

## Overview

This sample measures the issue performance of five point-to-point interfaces on the SIMT path: `WriteNbi`, `WriteValueNbi`, `WriteWithNotifyNbi`,
`AtomicFAA`, and `AtomicCAS`, covering both immediate and deferred publication, and reports the WQE issue latency and completion bandwidth separately.

This is a point-to-point benchmark: `<nranks>` can be >= 2, but only rank 0 (the sender) and rank 1 (the receiver) establish the channel and run the timing;
the other ranks print `SKIP` and exit directly. Keeping a single sender is intentional — multiple concurrent senders contend for the link and SQ resources,
so the measured latency and bandwidth are no longer comparable. For multi-card send/receive validation, use the
[`simt_urma`](../04_simt_urma/README.md) functional sample.

All operations are issued by a single lane, and one channel must be driven by one lane.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
simt_urma_perftest
├── CMakeLists.txt               // Build project file
├── main.cpp                     // Host-side communicator, memory registration, and timing flow
├── op_kernel.cpp                // SIMT Kernel-side URMA interface calls
├── simt_urma_perftest_common.h  // Definitions shared by Host and Kernel
├── README.md                    // Chinese sample documentation
└── README_en.md                 // English sample documentation
```

## Sample Description

### Functionality Description

Five interfaces x two publication strategies:

| `api` | Description |
| --- | --- |
| `write` | `WriteNbi`; the WQE points at a local buffer through an SGE, and the payload length is configurable |
| `write_value` | `WriteValueNbi`; an 8-byte payload inlined in the WQE, without reading the local buffer |
| `notify` | `WriteWithNotifyNbi`; writes a remote signal word after the payload, and the payload length is configurable |
| `faa` | `AtomicFAA`; accumulates into the same remote 8-byte word |
| `cas` | `AtomicCAS`; compare-and-swap on the same remote 8-byte word |

| `commit-mode` | Description |
| --- | --- |
| `immediate` | Every WQE uses `commit=true` and rings its own doorbell |
| `last` | All WQEs except the last one use `commit=false`, and the last one publishes the whole batch |

The two strategies also differ in SQ footprint: a deferred `WriteNbi` occupies one basic block, while an immediate one splits the payload into two SGEs
to fill the 128-byte DWQE window and occupies two. The other four interfaces occupy a fixed 2 BB regardless of strategy. `SqBlocksPerWqe` in the log
reports the actual footprint.

The timing flow is: issue the interface `warmup` times, then `Drain` (not timed); then issue it `iterations` times and record `IssueTime`
(counts only the timed WQE issues, used to compare issue latency between interfaces). After `Drain` completes, record `CompletionTime` (used only to compute
`CompletionBandwidth`). `DrainStatus` and `Completed` are used to confirm that the run completed correctly.

Consecutive WQEs rotate over multiple slots to avoid landing on the same cache line — otherwise the measurement reflects the remote memory system rather than the issue path.
The two atomic interfaces are the exception: they must target the same word for the accumulation to be meaningful, so they do not rotate.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Point-to-point benchmark; only rank 0 sends and rank 1 receives |
| Invocation Method | Direct URMA interface calls within the SIMT Kernel |
| Tested Interfaces | `write`, `write_value`, `notify`, `faa`, `cas` |
| Publication Strategy | `immediate` (default), `last` |
| Supported Rank Count | Any >= 2 (rank 2 and above do not participate in communication) |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   ranks exchange root info through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`) and
   create the communicator; only rank 0 and rank 1 join the communicator, and the other ranks exit directly.
2. Rank 0 and rank 1 register their memory, create the channel, and exchange memory addresses.
3. Rank 0 issues the interface `warmup` times and calls `Drain`, then issues it `iterations` times while timing, and calls `Drain` again.
4. Rank 0 records `IssueTime`, `CompletionTime`, and `Completed`; rank 1 validates the landed data.
5. Outputs the RESULT line.

## Compilation and Execution

Perform the following steps in the root directory of this sample to compile and run it. This sample supports NPU run mode only.

- Configure environment variables

  The sample builds against the asc-comm headers installed into the CANN directory. If they are not yet installed, first generate and install the development verification
  run package from the repository root. For parameter details, see [`docs/en/compilation/build_and_test.md`](../../../../docs/en/compilation/build_and_test.md):

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_9.3.0_linux-<arch>.run --full
  ```

  Then load the CANN environment variables:

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If not specified, the default installation directory is `/usr/local/Ascend`.

- Run the sample

  Execute the following commands in the sample directory. The executable internally forks all rank processes, so it can be run directly:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 write
  ```

- Launch parameter description

  ```bash
  ./build/simt_urma_perftest <ip:port> <nranks> <write|write_value|notify|faa|cas> [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of rank processes to launch; must be >= 2 (only rank 0/1 participate in the test) |
  | `--iterations` | `1024` | Number of timed WQEs |
  | `--warmup` | `100` | Number of warmup WQEs |
  | `--payload-bytes` | `4096` | Payload bytes for `write` and `notify`, minimum 2. `write_value` is always 8 bytes, and `faa`/`cas` are always 8 bytes, so these three ignore this option |
  | `--commit-mode` | `immediate` | `immediate` (one doorbell per WQE) or `last` (defer all but the last WQE) |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT products |

- Expected output

  ```text
  RESULT | Path=SIMT-t1 | API=write | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | SqBlocksPerWqe=2 | Slots=64 | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
  [rank 1] simt_urma_perftest write PASS
  ```

  The sample succeeds when `DrainStatus=0`, `Completed` matches `WqeCount`, and rank 1 prints `PASS`.

## Notes

- Running the sample requires at least two NPUs; a single-card environment supports compilation verification only.
- `--payload-bytes` must be at least 2: an immediate `WriteNbi` splits the payload into two SGEs, and a single byte would make the first segment zero-length,
  which URMA does not accept.
- The receiving side only validates `write` and `write_value`: the `notify` signal word shares one slot with its payload, and the two atomic
  interfaces rewrite the target word on every iteration, so the landed image depends on the iteration count and has no stable expected value. These two categories rely on
  `DrainStatus` and `Completed` to determine whether the run completed correctly.
- For a payload sweep, run the executable once for each of several `--payload-bytes` values (such as `8 16 64 256 1024 4096 16384 65536`).
  No further configuration is required.
- The build relies on the CANN ASC CMake utilities and, at the link stage, on the CANN `hcomm` library.
