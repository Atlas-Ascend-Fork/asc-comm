# SIMT Notify/FAA/CAS Basic Performance Sample

This directory tests the `Notify`, `FAA`, and `CAS` interfaces independently. Immediate mode supports `--lanes 1`
only. Last mode supports only `--lanes 1`; multi-lane (`--lanes 32/1024`) submission requires cooperation at
the kernel source level and is not implemented in this version.

Rank 0 issues operations and measures latency; rank 1 holds registered memory and verifies results. Every operation is
a single-lane immediate post with `commit=true`, minimizing synchronization overhead to isolate issue latency.

## Run

The communicator is created with `HcclCommInitClusterInfo`, and running requires an HCCL rank table. Each invocation
tests one interface with configurable iteration count, warmup rounds, payload size (Notify only), and commit mode.

Notify:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 --lanes 1 --commit-mode immediate
```

FAA:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json faa \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode immediate
```

CAS:

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json cas \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode immediate
```

Each command executes one interface and one data size; the script does not scan or combine scenarios internally.

## Output

Every iteration prints:

```text
[Notify|FAA|CAS] iteration=<N>: <elapsed_ns> ns
```

At the end, a summary line reports the median, mean, standard deviation, min, max, and outlier-trimmed mean:

```text
RESULT | Mode=<api> | CommitMode=<mode> | Lanes=<N> | PayloadBytes=<size> | ...
       IterationCount=<iterations> | OutlierTrimRatio=<ratio> | Median=<ns> ns | ...
       Mean=<ns> ns | StdDev=<ns> ns | Min=<ns> ns | Max=<ns> ns | TrimmedMean=<ns> ns
```

## Build

```bash
cd examples/simt_notify_atomic_perf
bash build.sh
```

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

## Notes

- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required for runtime execution; single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library at link time.
- Ranks synchronize through local temporary files under `$TMPDIR`.
