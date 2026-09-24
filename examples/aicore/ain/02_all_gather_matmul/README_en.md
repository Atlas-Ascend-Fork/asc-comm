# AIN + Blaze AllGatherMatmul

[简体中文](README.md) | English

This example composes AIN `Get` on AIV with Blaze `BlockMmad` on AIC in a single 1:1 MIX kernel:

```text
C_r[P*M,N] = Concat(A_0[M,K], ..., A_(P-1)[M,K]) @ B_r[K,N]
```

Each rank may have different B. C is ordered by source rank. The initial scope is Ascend 950PR/950DT,
single-node ranks in one connected fabric group, FP16/BF16 input and output with FP32 accumulation,
contiguous ND, no transpose or bias. M may be unaligned; N/K must be positive multiples of 16.
This is a standalone example, not an ACLNN operator or a quantized MC2 replacement.

## Dependencies and build

Use a CANN installation containing the AIN, HCCL team/window APIs and their implementations.
See [VALIDATION.md](VALIDATION.md) for the actual tested environment.
Blaze remains an external header dependency; ops-transformer is not needed.

```bash
source /path/to/cann/set_env.sh
git clone https://gitcode.com/cann/ops-tensor.git /path/to/ops-tensor
git -C /path/to/ops-tensor checkout e6cd0c20c716813ac4c76f2dba7152f63a4514ab
git -C /path/to/ops-tensor submodule update --init --recursive include/tensor_api
# Tensor API revision: fd51475d8ceb26eb21befffe4391528bc16726a3
cmake -S . -B build -DOPS_TENSOR_ROOT=/path/to/ops-tensor
cmake --build build -j
```

The CMake target uses ASC/dav-3510 and the host PIPE_FIX compatibility header supplied by Blaze.
No custom OPP package needs installation.

## Scheduling and memory

AIC first computes its local A, directly into its rank's output rows. Meanwhile AIVs pull M chunks
from peer windows. Each local peer channel has exactly one owner. That owner submits all assigned
Gets and waits per channel using `FlushAsync`/`Wait`. All local AIVs then synchronize and notify
their paired AIC. Remote tiles across peers are distributed together to the compute cores.

The next communication round overlaps with the previous computation round. Two ready/free slots
bound the notification count; all cores, including idle cores, participate. There is no growing
per-round flag ID. AIV mode-0 flag 0 is the local barrier, mode-2 flags 2/3 are ready and 4/5 are free.
The fused kernel uses batch scheduling. AIC waits on MTE2 before reading received A, whose Tensor
disables the L2 cache hint. All final acknowledgements are consumed.

The receive window is a full rank-major allocation of `2*P*M*K` bytes, without in-kernel reuse.
The self slot is intentionally unused. No packing or output reordering is needed. Host collectives
before launch and after stream completion protect remote A lifetime. Inputs stay immutable during
each invocation. Host collectives are outside event timing. The shared TCP control channel enables
`TCP_NODELAY` to avoid delayed-ACK launch skew. Residual launch skew can still enter collective
wait time; there is no inter-rank barrier per chunk.

Blaze tiles start at M/N=256, K L0/L1=64/128, with two L1 stages. Communication tile M is chosen
to supply at least one compute wave, capped by M and the 256 MiB Get limit. Non-final communication
tiles must have a multiple of 256 rows. This heuristic is a starting point, not a performance guarantee.

## Run

```bash
./build/ain_all_gather_matmul tcp://127.0.0.1:29640 2 257 80 272 \
  --devices 1,2 --tile-m 256 --random --warmup 1 --iters 5
```

Positional arguments are `endpoint ranks M K N`. The executable forks local ranks without MPI.
Select devices in the **same physical connectivity group**. On A5_4p, groups are 0–3 and 4–7:
use `--devices 0,1,2,3` for four ranks, not `1,2,3,4`. IDs refer to the current ACL logical device
namespace; account for any externally configured visibility mapping.

| Option | Default | Meaning |
| --- | --- | --- |
| `--devices` | `0,...,P-1` | One unique ACL device ID per rank |
| `--dtype` | fp16 | fp16 or bf16 |
| `--mode` | both | both runs serial then fused |
| `--tile-m` | auto | Communication chunk rows |
| `--cores` | device AIC count | Launch core count |
| `--warmup`, `--iters` | 10, 100 | Warmup and measurement counts |
| `--verify-iters` | 2 | Reuse resources with changed inputs and validate |
| `--random` | off | Full-K random data and O(PMKN) CPU reference, for small shapes |
| `--trace` | off | One additional instrumented fused invocation, outside measurements |
| `--timeout` | 300 seconds | Whole process-group timeout including CPU checks |
| `--delay-rank`, `--delay-ms` | none, 0 | Delay one rank after input readiness |

Modes: `fused` is one MIX kernel; `serial` is AIN Get then the same Blaze compute kernel;
`hccl` is HCCL AllGather then that compute kernel; `comm` checks AIN only; `matmul` checks Blaze
with host-filled inputs. HCCL uses the environment's default algorithm.

Default fixtures have a K period of 16, with rank/row/column/iteration-dependent values, allowing
full-output golden checks in O(PMN*16). `--random` uses independent full-K inputs. Values are
exactly representable multiples of 1/16. Received A is checked bitwise. C is checked against an
FP32 reference rounded to output dtype, with FP16 tolerance `0.001 + 0.001*abs(expected)` and
BF16 tolerance `0.001 + 0.008*abs(expected)`. Non-finite values fail. Inputs are changed during
validation; a final output check follows measurement.

`PERF` reports median/P95 of each iteration's maximum rank device-event duration. Initialization,
copies, CPU checks and TCP collectives are excluded. Serial/cascade durations include the gap
between kernels. These are steady-state device intervals, not globally clock-aligned envelopes
or end-to-end host request latency.
`host_submit_skew_p95_us` reports the P95 spread of host monotonic timestamps immediately before
start-event submission. It is not device kernel-start skew and is not subtracted from event duration.

`TRACE` records paired AIV0/AIC0 phase intervals using the same-device system counter
(1000 cycles/us on 950). Compute intervals synchronize pipelines; remote readiness waits are
outside compute intervals. Instrumentation changes timing and is excluded from `PERF` samples.
Intersecting intervals prove overlapping phases, not that network/Cube hardware is busy every
cycle. Do not compare absolute counter values across devices.

## Checks

```bash
# Portable checks without CANN: NOT device execution
cmake -S . -B /tmp/ain-agmm-host -DAGMM_HOST_TESTS_ONLY=ON
cmake --build /tmp/ain-agmm-host
ctest --test-dir /tmp/ain-agmm-host --output-on-failure

# NPU regression; output directory must not already exist
python3 tests/run_device_tests.py --binary build/ain_all_gather_matmul \
  --devices 0,1,2,3 --output-dir device-results
```

The suite checks both dtypes, tails, 18 communication rounds, one core owning multiple peers,
delayed rank entry, independent communication/computation and the HCCL cascade. Logs and
`results.json` preserve commands and exit status. The process group reports PASS only if all
ranks succeed. A faster result for a particular shape is not a general claim against traditional
MC2; compare that operator separately with matching layout, dtype and communication engine.
