# Hcomm SIMT URMA Performance Sample

Measures the issue performance of five point-to-point interfaces on the SIMT path: `WriteNbi`,
`WriteValueNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`. Rank 0 sends and times; rank 1
only holds registered memory and verifies what landed.

Every case is driven by a single lane: one channel must be driven by one lane.

## Only the First Two Ranks Participate

This is a point-to-point benchmark: `<nranks>` may be >= 2, but only rank 0 (sender) and rank 1
(receiver) build the channel and run the benchmark. Every other rank prints `SKIP` and exits without
joining the communicator. Keeping a single sender is deliberate — concurrent senders contend for the
link and for SQ space, and the resulting latency and bandwidth numbers are no longer comparable.

For multi-card send/receive validation use [`simt_urma`](../04_simt_urma/README_en.md), which is a ring
topology in which every rank participates.

## What Is Measured

Five interfaces x two publication strategies:

| `api` | Description |
| --- | --- |
| `write` | `WriteNbi`; the WQE points at a local buffer through an SGE, payload length is configurable |
| `write_value` | `WriteValueNbi`; an 8-byte payload carried inline in the WQE, the local buffer is never read |
| `notify` | `WriteWithNotifyNbi`; writes a remote signal word after the payload, payload length is configurable |
| `faa` | `AtomicFAA`; accumulates into a single remote 8-byte word |
| `cas` | `AtomicCAS`; compare-and-swap on a single remote 8-byte word |

| `commit-mode` | Description |
| --- | --- |
| `immediate` | Every WQE uses `commit=true` and rings its own doorbell |
| `last` | All but the last use `commit=false`; the final post publishes the whole batch |

The two strategies differ in SQ footprint as well as doorbell count: a deferred `WriteNbi` occupies
one basic block, while a committed one splits its payload across two SGEs to fill the 128-byte DWQE
window and occupies two. The other four interfaces are fixed 2-BB WQEs either way. `SqBlocksPerWqe`
in the log reports the actual footprint.

Consecutive WQEs rotate over several slots so they do not all land on the same cache line — otherwise
the measurement reflects the remote memory system rather than the issue path. The two atomics are the
exception: they must target one word for the accumulation to mean anything, so they do not rotate.

## Timing Flow

```text
run the interface warmup times
Drain                         not timed

begin = clock()
run the interface iterations times
issueEnd = clock()
Drain
completionEnd = clock()
```

`IssueTime` and `AverageIssue` cover only the timed WQE submissions and are used to compare issue
latency between interfaces. `CompletionTime` runs from the start of the timed submissions to the end
of `Drain`, and is used only to compute `CompletionBandwidth`. `DrainStatus` and `Completed` confirm
the run actually completed correctly.

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

  The executable forks all rank processes internally. Only rank 0 and rank 1 participate in the
  benchmark; other ranks print `SKIP` and exit.

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 write
  ```

  ```text
  ./build/simt_urma_perftest <ip:port> <nranks> <write|write_value|notify|faa|cas> [options]
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | Required | Control channel address that rank 0 listens on (`tcp://ip:port`) |
  | `<nranks>` | Required | Number of rank processes; must be >= 2 (only rank 0/1 participate) |
  | `--iterations` | `1024` | Timed WQE count |
  | `--warmup` | `100` | Warmup WQE count |
  | `--payload-bytes` | `4096` | Payload bytes for `write` and `notify`; minimum 2; ignored by the other three interfaces |
  | `--commit-mode` | `immediate` | `immediate` (one doorbell per WQE) or `last` (defer all but the last) |

- Build Options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture for Ascend 950PR/Ascend 950DT |

- Example Commands

  ```bash
  # Single-point write
  ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 write

  # Deferred publication
  ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 write --commit-mode last

  # Inline write
  ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 write_value

  # Notify and the atomics
  ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 notify --payload-bytes 4096
  ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 faa
  ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 cas
  ```

  For payload sweeps, run the executable multiple times with different `--payload-bytes` values
  (e.g. `8 16 64 256 1024 4096 16384 65536`).

## Output

```text
RESULT | Path=SIMT-t1 | API=write | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | SqBlocksPerWqe=2 | Slots=64 | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
[rank 1] simt_urma_perftest write PASS
```

## Constraints

**`--payload-bytes` must be at least 2.** A committed `WriteNbi` splits its payload into
`firstLen = len >> 1` and `lastLen = len - firstLen`; a single byte would give the first segment a
zero length, which URMA does not accept.

**Only `write` and `write_value` are verified on the receiving side.** The `notify` signal word shares
a slot with its payload, and the two atomics rewrite their target word on every iteration, so the
landed image depends on the iteration count and there is no stable expected value to compare against.
Those cases rely on `DrainStatus` and `Completed` to determine whether the run completed correctly.

## Runtime Constraints

- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs on the same host are required to run; single-NPU environments only support
  compilation verification.
- The build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library.
