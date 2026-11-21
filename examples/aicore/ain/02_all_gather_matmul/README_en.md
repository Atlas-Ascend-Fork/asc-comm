# AIN + Blaze AllGatherMatmul Sample

[简体中文](README.md) | English

## Overview

This example combines AIN `get` and Blaze `BlockMmad` in a single MIX kernel with AIC:AIV = 1:1,
demonstrating communication-computation overlap. The Host uses HCCL to create the communication
domain and symmetric window/team resource interfaces; the AIV submits AIN communication on the
data plane, and the AIC performs matrix multiplication.

See the [code execution flow guide](ain_blaze_agmm_flow_en.md) for the overall program flow diagram
mapped to functions, the AIV/AIC collaboration flow diagram inside the fused kernel, and an
explanation of the data layout and ready/free notifications.

## Supported Products and Environment Requirements

This example targets Ascend 950PR&950DT products with the `dav-3510` architecture and supports
single-node NPU execution only. The CANN development kit must provide the AIN and HCCL
team/window interfaces with their runtime libraries, and support ASC compilation.
For dependency and build environment details, see
[Dependencies and Compatibility](../../../../docs/en/compilation/dependencies.md).

## Directory Structure

```text
02_all_gather_matmul/
├── CMakeLists.txt             # Standalone build and host test entry
├── main.asc                   # Host resource management, kernel launch, and result validation
├── kernel.h                   # AIN communication and Blaze matrix multiplication
├── tiling.h                   # Chunking and task distribution parameters
├── options.h                  # Command-line argument parsing
├── reference.h                # Input generation and reference result computation
├── tests/                     # Host tests and device regression scripts
├── ain_blaze_agmm_flow.md     # Function call and synchronization flow guide (Chinese)
├── ain_blaze_agmm_flow_en.md  # Function call and synchronization flow guide (English)
├── README.md                  # Chinese sample documentation
└── README_en.md               # English sample documentation
```

## Features and Constraints

Each rank `r` provides `A_r[M,K]` and `B_r[K,N]` and obtains:

```text
C_r[P*M,N] = Concat(A_0, ..., A_(P-1)) @ B_r
```

`B_r` may differ across ranks, and the output is ordered by source rank. This version supports:

- Ascend 950PR/950DT, `dav-3510`, 2–64 ranks within the same interconnect group on a single node
  (and no more than the number of available devices).
- FP16/BF16 input and same-type output, FP32 accumulation, contiguous ND memory, no transpose,
  no bias.
- `M>0`; N/K must be positive multiples of 16 elements. M may be unaligned; tail blocks for
  communication and Cube tiling are supported.
- Standalone example interfaces; no ACLNN registration, quantization, variable-length rank inputs,
  or public gather_out output.

**Devices must be selected according to the actual interconnect topology.** If devices 0–3 and
4–7 belong to two separate interconnect groups, a four-rank run should use `--devices 0,1,2,3` or
`--devices 4,5,6,7`, not `1,2,3,4`. IDs are logical device IDs in the current ACL environment;
do not keep using the original physical IDs after a visibility mapping has been configured.

## Data Flow and Synchronization

1. Register a send window for each rank's A and a receive window for the `[P,M,K]` receive buffer,
   and create the AIV/UB_CTP team.
2. Assign remote peers to AIVs; each local peer channel has exactly one submitter and one
   completion waiter.
3. The AIC first computes the local `A_r @ B_r` directly; meanwhile, AIVs issue Gets to all remote
   peers in M-sized chunks.
4. In each round, an AIV uses `flush_async + wait` to wait on its own peer channels, then performs
   the local AIV barrier. Each AIV notifies its paired AIC that remote data for this round is
   ready; the AIC distributes tasks across the M/N tiles of all peers for that round.
5. Gets of the next round overlap with remote computation of the current round. Two ready/free
   slots bound the prefetch depth; all notifications are drained at the end.

The receive GM uses the full rank-major space; data is not overwritten cyclically within the same
kernel. The local receive slot is unused: local computation reads the original A directly and
remote computation reads the receive GM directly, with no extra pack/unpack. The per-rank receive
space is `2*P*M*K` bytes.

AIN completion waiting, local inter-core notification, and AIC MTE2 load ordering are handled
separately:

- Each Get is submitted immediately and waited on before the same channel is reused in the next
  round; a single Get does not exceed 256 MiB.
- mode 0 / flag 0 is used for the all-AIV synchronization; mode 2 / flags 2 and 3 are used for
  ready, and flags 4 and 5 for free. Flag IDs do not grow with the number of chunks. Idle compute
  cores also participate in consuming and returning notifications.
- The fused kernel uses `__schedmode__(1)`; the AIC waits on blocking MTE2, and the Tensor of the
  received A disables the L2 cache hint.
- The Host barriers before each invocation and aggregates per-rank times after stream completion,
  protecting the remote input lifetime. A stays unchanged during this period. TCP synchronization
  calls are outside device events; the shared control channel enables `TCP_NODELAY` to avoid
  millisecond-level launch skew caused by delayed ACK. Residual launch skew may still enter the
  collective wait time. There is no per-round cross-card barrier.

The compute part reuses Blaze Basic `BlockMmad`. The default Cube M/N tile is 256, K L0/L1 is
64/128, with L1 double buffering. The communication tile is automatically chosen as an M chunk
that provides at least one round of AIC work, bounded by M and the Get length limit; it can also
be fixed with `--tile-m`. Rows of every non-final communication block must be a multiple of 256.
The automatic value is an initial heuristic, not an optimal-performance guarantee.

## Build

Load a CANN development kit environment that meets the interface requirements above before
building. The Blaze dependency version and initialization are as follows.

Blaze is an external header-only dependency: it is not copied into asc-comm, and ops-transformer
is not modified. Pinned dependency versions:

```text
ops-tensor: e6cd0c20c716813ac4c76f2dba7152f63a4514ab
Tensor API submodule: fd51475d8ceb26eb21befffe4391528bc16726a3
```

```bash
CANN_ENV="/path/to/cann/set_env.sh"  # Adjust to the actual installation path
OPS_TENSOR_ROOT="/path/to/ops-tensor"
source "$CANN_ENV"
git clone https://gitcode.com/cann/ops-tensor.git "$OPS_TENSOR_ROOT"
git -C "$OPS_TENSOR_ROOT" checkout e6cd0c20c716813ac4c76f2dba7152f63a4514ab
git -C "$OPS_TENSOR_ROOT" submodule update --init --recursive include/tensor_api

# Run in this example directory
cmake -S . -B build -DOPS_TENSOR_ROOT="$OPS_TENSOR_ROOT"
cmake --build build -j
```

CMake checks the Blaze/Tensor API dependencies, builds through CANN ASC, and reuses the host
`PIPE_FIX` compatibility header from the Blaze example. The default ASC target is `dav-3510`.
No custom OPP package needs to be installed.

## Run and Comparison Modes

```bash
# Two ranks, small shape with full random golden, serial vs fused comparison
./build/ain_all_gather_matmul tcp://127.0.0.1:29640 2 257 80 272 \
  --devices 1,2 --tile-m 256 --random --warmup 1 --iters 5

# Four-rank steady-state measurement; adjust devices to the actual interconnect group of this machine
./build/ain_all_gather_matmul tcp://127.0.0.1:29640 4 1024 4096 4096 \
  --devices 0,1,2,3 --tile-m 256 --dtype bf16 --trace
```

Positional arguments are `endpoint ranks M K N`. The process manager forks all ranks locally;
mpirun is not required.

| Option | Default | Meaning |
| --- | --- | --- |
| `--devices` | `0,...,P-1` | ACL device ID per rank; must be unique and topology-reachable |
| `--dtype` | `fp16` | `fp16` / `bf16` |
| `--mode` | `both` | `both` runs serial then fused |
| `--tile-m` | auto | Communication M chunk; the final chunk uses the actual row count |
| `--cores` | device AIC count | Launched core count; use 1 to verify multiple peers per core |
| `--warmup` / `--iters` | 10 / 100 | Warmup / steady-state iteration counts |
| `--verify-iters` | 2 | Number of runs with changed inputs and validation; the final result is validated again after measurement |
| `--random` | off | Full-K random inputs with O(PMKN) CPU golden; recommended for small shapes only |
| `--trace` | off | Runs one extra diagnostic fused kernel with system cycle recording |
| `--timeout` | 300 s | Timeout of the whole process group, including initialization, validation, and measurement |
| `--delay-rank` / `--delay-ms` | none / 0 | Delays the launch of the specified rank after inputs are ready; checks input lifetime |

| Mode | Behavior |
| --- | --- |
| `fused` | AIN Get and Blaze computation in one MIX kernel |
| `serial` | The same Blaze compute kernel starts after the AIN Get kernel completes |
| `hccl` | HCCL AllGather followed by the same Blaze compute kernel; HCCL uses the environment's default algorithm |
| `comm` | Standalone AIN Get; validates received remote data |
| `matmul` | Host fills the full inputs; validates Blaze computation standalone |

Default data has a period of 16 over K, with rank/row/column/iteration participating in data
generation, allowing fast validation of **all output elements** on large shapes. `--random`
generates data independently over all K indices. Both kinds of inputs are exactly representable
multiples of 1/16. The CPU golden uses FP32 accumulation and converts to the output type;
FP16/BF16 comparison thresholds are `0.001 + 0.001*abs(expected)` and
`0.001 + 0.008*abs(expected)` respectively, and non-finite values fail. Received remote A is
compared element-wise bit by bit.

`PERF` records the maximum device-event duration across all ranks for each iteration and then
reports median/P95. Resource creation, input copies, CPU validation, and TCP synchronization are
outside the timed intervals. serial/hccl durations include the gap between the two kernels.
These are steady-state device execution times, not end-to-end request latency including Host
domain setup, and not a globally clock-aligned envelope across devices.
`host_submit_skew_p95_us` records the P95 Host monotonic-clock skew immediately before each
rank's start event, used to check Host launch consistency; it is not device kernel-start skew and
is not directly subtracted from event durations.

`TRACE` records system-cycle intervals of paired AIV0/AIC0 in a standalone diagnostic kernel;
on 950, 1000 cycles = 1 us. Compute phases record the pipelines synchronized before and after;
ready waits are not counted in the corresponding remote compute phase. Instrumentation changes
timing and is **never mixed into PERF samples**; intersecting phase intervals indicate phase
parallelism and must not be interpreted as the network link/Cube units being busy simultaneously
in every cycle. Absolute cycle values of different ranks are not used for cross-card comparison.

## Functional Verification

```bash
# Runs on macOS/Linux without CANN; does not represent NPU coverage
cmake -S . -B /tmp/ain-agmm-host -DAGMM_HOST_TESTS_ONLY=ON
cmake --build /tmp/ain-agmm-host
ctest --test-dir /tmp/ain-agmm-host --output-on-failure

# Target environment: select devices within the actual interconnect group; the output directory must not exist
python3 tests/run_device_tests.py --binary build/ain_all_gather_matmul \
  --devices 0,1,2,3 --output-dir device-results
```

Device tests cover FP16/BF16, M/N/K tile tail blocks, 18 communication chunks, one core owning
multiple peers, delayed rank entry, independent communication and independent computation, the
HCCL comparison, and changed inputs when reusing resources. Every command and log is saved in the
output directory, and `results.json` keeps exit codes and verdicts. The final `Status=PASS` is
printed only when all rank validations pass and the process group succeeds.

### Expected Result

During normal execution, each rank prints its validation result and the process group prints the
final status. For example:

```text
CHECK rank=0 mode=fused seed=1 errors=0
RESULT | Example=ain_all_gather_matmul | Status=PASS
```

Check that all ranks report `errors=0`, the final `Status=PASS` is printed, and the process exit
code is 0.

## Manual Functional and Performance Testing

Run the following commands in order within the same Bash session on the target Linux host. For
remote execution, log in to the target host over SSH first. Complete the build above first, or
use an existing binary that matches the current source code and CANN package. All paths are
configured centrally; no fixed host or installation directory is assumed.

### 1. Configure the Environment and Capture the Context

Adjust the paths and device list below. `WORK` is the working directory of this test; source code
and build artifacts may reside elsewhere, specified through `CASE_DIR` and `BIN` respectively.
This procedure uses four cards within the same interconnect group.

```bash
CANN_ENV="/path/to/cann/set_env.sh"
WORK="/path/to/test-work"
CASE_DIR="/path/to/asc-comm/examples/aicore/ain/02_all_gather_matmul"
BIN="$CASE_DIR/build/ain_all_gather_matmul"
DEVICES="4,5,6,7"  # Adjust per the current ACL numbering and interconnect topology, e.g. 0,1,2,3
BASE_PORT=30100     # Ensure BASE_PORT through BASE_PORT+35 are not occupied by other tests

source "$CANN_ENV"
set -e
set -o pipefail     # Stop subsequent steps when a test fails, even if logs are saved through tee
mkdir -p "$WORK"
cd "$WORK"
test -x "$BIN"
test -f "$CASE_DIR/tests/run_device_tests.py"
npu-smi info
ldd "$BIN"
```

Fill in the paths above as absolute paths. Before continuing, confirm that the four selected
cards are not occupied by other workloads and that `ldd` reports no `not found`, with the
actually loaded CANN libraries coming from the expected installation directory. Run performance
tests only while the selected devices are idle. If a device visibility mapping is configured,
verify the mapped ACL device IDs first. Do not run across four-card groups that are not
interconnected.

```bash
RESULT_DIR=$(mktemp -d "$WORK/ain-agmm-results.XXXXXX")
{
  date -Iseconds
  uname -a
  echo "CANN_ENV=$CANN_ENV"
  echo "ASCEND_HOME_PATH=${ASCEND_HOME_PATH:-}"
  echo "BIN=$BIN DEVICES=$DEVICES"
  git -C "$CASE_DIR" rev-parse HEAD
  git -C "$CASE_DIR" status --short
  for component in runtime asc-devkit hccl hcomm; do
    version_file="${ASCEND_HOME_PATH:-}/share/info/$component/version.info"
    echo "component=$component version_file=$version_file"
    if [ -f "$version_file" ]; then
      cat "$version_file"
    else
      echo "Version file missing; add records according to the current installation layout"
    fi
  done
  sha256sum "$BIN"
  ldd "$BIN"
  npu-smi info
} > "$RESULT_DIR/environment.log" 2>&1
echo "Result directory: $RESULT_DIR"
```

Create a new `RESULT_DIR` for every rerun to avoid overwriting existing results. Package
directory names do not necessarily equal component versions, so keep component version
information and the binary checksum; also record the source revision the binary was built from.

### 2. Functional Testing

First run the four-rank, small-shape random-input smoke test to check the AIN serial and fused
paths:

```bash
"$BIN" "tcp://127.0.0.1:$BASE_PORT" 4 257 80 272 \
  --devices "$DEVICES" --dtype fp16 --mode both --tile-m 256 \
  --random --verify-iters 2 --warmup 1 --iters 5 --timeout 180 \
  2>&1 | tee "$RESULT_DIR/functional-smoke.log"
```

The pass criteria are a process exit code of 0, `errors=0` in every rank's `CHECK` output, and
the final `Status=PASS`. Then run the full device regression:

```bash
python3 "$CASE_DIR/tests/run_device_tests.py" \
  --binary "$BIN" --devices "$DEVICES" \
  --output-dir "$RESULT_DIR/functional" --port "$((BASE_PORT + 10))" \
  2>&1 | tee "$RESULT_DIR/functional-suite.log"
```

With four cards provided, 11 cases run in total, covering the verification scenarios above;
two-rank cases use the first two cards of the device list, and four-rank cases use all four
cards. Check that all cases are `PASS`, and keep `functional/results.json` and the per-case
logs. The script exits at the first failure; cases that did not run cannot count as passed.
The directory specified by `--output-dir` must not exist yet.

### 3. Performance Testing: Six Comparisons

After the functional tests pass, test the three modes for FP16/BF16 in order while the devices
are idle and the environment stays consistent:

| Mode | Comparison meaning |
| --- | --- |
| `serial` | AIN AllGather and Blaze Matmul executed serially |
| `fused` | AIN and Blaze Matmul overlapped within one MIX kernel |
| `hccl` | HCCL AllGather and Blaze Matmul executed serially |

Fix `M=1024, K=4096, N=1024` per rank, so the four-rank output is `[4096,1024]`. Each group runs
10 warmup and 100 measured iterations, with the communication chunk fixed at 256 rows to keep
automatic chunking differences out of the comparison.

```bash
mkdir "$RESULT_DIR/performance"
port=$((BASE_PORT + 30))
for dtype in fp16 bf16; do
  for mode in serial fused hccl; do
    echo "Test: dtype=$dtype mode=$mode port=$port"
    "$BIN" "tcp://127.0.0.1:$port" 4 1024 4096 1024 \
      --devices "$DEVICES" --dtype "$dtype" --mode "$mode" --tile-m 256 \
      --verify-iters 2 --warmup 10 --iters 100 --timeout 600 \
      2>&1 | tee "$RESULT_DIR/performance/${dtype}_${mode}.log"
    port=$((port + 1))
  done
done
npu-smi info > "$RESULT_DIR/devices-after.log"
```

Performance tests use the default periodic inputs and still validate all output elements; do not
add `--random` on large shapes to avoid the expensive CPU golden. Do not add `--trace` here; to
observe overlapping phases, run the diagnostic separately and keep an independent log. Do not run
different test groups concurrently.

### 4. Summary and Interpretation

```bash
grep -H '^PERF ' "$RESULT_DIR"/performance/*.log \
  | tee "$RESULT_DIR/performance-summary.txt"
grep -H '^RESULT ' "$RESULT_DIR"/performance/*.log
```

All six logs should end with `Status=PASS`. For each dtype, record `max_rank_median_us`,
`max_rank_p95_us`, and `host_submit_skew_p95_us` of the three modes, all in microseconds. The
first two are the median and P95 after taking the slowest rank each iteration; see the `PERF`
notes above for the exact timing scope. The third only helps check Host submit skew and cannot be
directly subtracted from device durations.

For the same dtype, using medians:

- Duration reduction relative to AIN serial: `(1 - fused / serial) * 100%`.
- Duration reduction relative to HCCL serial: `(1 - fused / hccl) * 100%`.
- Speedup: `baseline duration / fused duration`; below 1 means fused is slower.

Compare only results whose validation passed, whose devices were free of other workloads, and
whose shape/dtype/card count/chunking and CANN environment were identical. If durations fluctuate
noticeably, keep the raw logs and repeat the whole six-group suite in a new result directory
instead of picking only the fastest round. Functional and performance conclusions must be based
on actual runs in the current environment.

## Notes

The gains for any specific shape must not be generalized as a blanket advantage over traditional
MC2; the traditional MC2 operator still needs a separate comparison with matching shape, dtype,
layout, and communication engine. This example does not label the HCCL cascade as traditional MC2
performance.
