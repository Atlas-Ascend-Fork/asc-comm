# MC2 Algorithm Performance Tool Usage Guide

English | [简体中文](../zh/usage.md)

This guide covers routine use of the built-in backend. See [Supported Operations](../../README_en.md#supported-operations) for operations and engines, and the [Multi-node Guide](./multinode.md) for execution on two hosts.

## Configure the Environment

Prepare a matching CANN SDK, Host HCCL (the host-side collective communication library), an MPICH-compatible MPI environment, Python 3.9+, CMake 3.19+, and a C++11 compiler. Devices must be available, and the current user must have read and write access to the operator package installation directory.

The CANN SDK must provide `Mc2GetSupportAlgList`, `Mc2SetCcDataLenRange`, and a matching `libmc2_client.so`. Default profiling (device performance data collection) requires SDK ACL collection APIs and `libmsprofiler.so`. CCU raw data export also requires a matching SDK `msprof`. AICPU analyzes raw captures directly without exporting a timeline.

Run the following in Bash. Replace only `CANN_ROOT`, `MPI_HOME`, and the source path on the last line with actual locations. Existing dependencies do not need to be reinstalled:

```bash
export CANN_ROOT=/usr/local/Ascend/cann
export MPI_HOME=/usr/local/mpi/mpich
if [ -f "${CANN_ROOT}/set_env.sh" ]; then
    source "${CANN_ROOT}/set_env.sh"
else
    source "${CANN_ROOT}/../set_env.sh"
fi
export ASCEND_HOME_PATH="${CANN_ROOT}"
export ASCEND_CANN_PACKAGE_PATH="${CANN_ROOT}"
export PATH="${MPI_HOME}/bin:${PATH}"
cd /path/to/asc-comm/tools/mc2_alg_bench
```

The build first looks for the makeself packaging tool in CANN, then on the system PATH. A separate setting is usually unnecessary. For a separate Host HCCL installation, add `--hccl-root /actual/installation/path` before `--`. With a standard installation layout, the CCU dependency `libhcomm.so` is linked automatically.

For the first run, go directly to [Run a Single-host Benchmark](#run-a-single-host-benchmark), use the AICPU example, then [Read Performance Results](#read-performance-results). Consult the parameter and query sections as needed.

## Place Command Arguments

The first `--` separates launcher arguments from test arguments. Arguments within each section generally have no ordering requirement, but each value must follow its option.

```text
bash run_alg_bench.sh [launcher arguments] -- [test arguments]
```

| Position | Argument | Meaning and Default |
| --- | --- | --- |
| Before `--` | `-n`, `-p` | Total ranks and devices per node, both defaulting to 8; a rank is a participating communication process |
| Before `--` | `--query-only` | List candidates without running benchmarks |
| Before `--` | `--comm-only` | Enable AICPU delayed-submission diagnostics, disabled by default |
| Before `--` | `--ccu-probes` | CCU boundary-probe collection, disabled by default; requires SDK support |
| Before `--` | `--skip-build` | Verify and reuse installed packages and cached binaries; otherwise build normally |
| Before `--` | `--output-dir` | New result directory, allocated automatically when omitted; existing jobs cannot be overwritten |
| Before `--` | `--no-profiling` | Disable profiling and analysis, retaining whole-operator timing and output validation |
| Before `--` | `--cann-root`, `--mpi-root`, `--hccl-root` | Select installation roots explicitly instead of using environment settings |
| After `--` | `--op` | Required operation name |
| After `--` | `-a` | Engine: `aicpu_ts` or `ccu_sched`, defaulting to `aicpu_ts` |
| After `--` | `-d` | Data type, defaulting to `fp32` (32-bit floating-point); built-in measurements use `fp32` |
| After `--` | `-o` | Reduction: `sum`, `prod`, `max`, or `min`, defaulting to `sum`; actual support depends on the SDK |
| After `--` | `-b`, `-e` | Starting and target ending byte lengths, defaulting to 32KiB and 256MiB |
| After `--` | `-f` or `-i` | Multiplicative factor or linear byte step, mutually exclusive; the default is `-f 2` |
| After `--` | `--alg` | Complete algorithm name returned by the query; repeatable, or omit to test all candidates |
| After `--` | `--warmup`, `--iterations` | Warmup and measured iterations, defaulting to 10 and 20; benchmark mode only |

Lengths accept suffixes such as `B`, `K`, `KB`, `KiB`, `M`, `MB`, and `MiB`, using powers of 1024. Set both `-b` and `-e` for a single point. If only one endpoint is set, the other keeps its default. Multiplicative scans include the first point at or above the target ending length; linear scans stop at or below it. The default range contains 14 points.

Requested bytes may differ from each rank's input and output sizes: AllGather uses the total received size, ReduceScatter uses the total input size, and AllToAll operations use the total sent size per rank. The backend checks divisibility by rank count and data type. AllToAllV uses evenly split data.

## Query and Select Algorithms

This step is optional. To inspect candidates before selecting an algorithm, use the same AICPU engine, two devices, and 32KiB point as the first benchmark:

```bash
bash run_alg_bench.sh -n 2 -p 2 --query-only -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

The command filters by the 32KiB point. Omit `-b/-e` to query all candidates for the current conditions, or use `-b 32K -e 128K` for a range. The query length is called `dataLen`; the rules are:

| User Input | Query Behavior |
| --- | --- |
| `--alg` specified, with or without data lengths | Query once with `dataLen=0` and check whether the requested names match |
| No algorithms or lengths specified | Query all candidates for the current conditions with `dataLen=0` |
| No algorithms specified, with a single point | Pass that point's byte length to the query |
| No algorithms specified, with a range | Query each expanded point and take the deduplicated union of algorithm names |

Query-only mode still requires real devices and a communicator. It does not build operator packages, warm up, measure, or generate a performance CSV. Results are printed and saved to `query_summary.json` in the run directory. It cannot be combined with `--comm-only`, `--analyze`, or warmup/measurement count options.

A benchmark queries again and measures only matched candidates. Unmatched names are reported as unsupported and skipped, without a separate tool-side fallback. Range mode tests the candidate union across the complete measurement length sequence. A returned candidate does not guarantee successful execution at every length.

To select an algorithm, copy its complete name from query results for the same operation and engine. For example, use the following only if the preceding AICPU query returns `sole[mesh]`:

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K --alg 'sole[mesh]'
```

## Run a Single-host Benchmark

The following commands use two devices and a 32KiB point. No algorithm is specified: the tool queries and benchmarks candidates for those conditions automatically. Omit `--skip-build` on the first run; a separate query-only command is unnecessary.

AICPU AllReduce:

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

CCU AllReduce:

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op allreduce -a ccu_sched -b 32K -e 32K
```

CCU AllToAllVWrite:

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op alltoallvwrite -a ccu_sched -b 32K -e 32K
```

AllToAllVWrite input, output, and query data types are all `fp32`. The SDK provides the communication window, whose capacity is checked. Validate on one host first; do not directly apply the two-host, 16-rank example. Actual support depends on the SDK query.

For other standard operations, replace `--op`. The launcher prepares the engine's operator package, compiles the benchmark, queries, warms up, measures, validates, and generates reports automatically. No separate build or analysis command is required. Each test entry validates its last measured output after the measured iterations finish.

Each invocation creates a new result directory; previous results need not be deleted. Add `--skip-build` before `--` to reuse a matching configuration. Rebuild normally after source, SDK, operator package, measurement mode, or profiling setting changes.

## Use Optional Diagnostic Modes

For AICPU delayed-submission diagnostics, add `--comm-only` before `--`:

```bash
bash run_alg_bench.sh -n 2 -p 2 --comm-only -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

This is equivalent to `--measurement-mode comm_only`; do not specify both. The launcher builds the matching package. Valid expansion and kernel cells show `NA`, while communication and bandwidth values remain available. This mode includes an artificial delay and does not strictly isolate framework overhead. CCU does not support it.

For CCU boundary-probe collection, add `--ccu-probes` before `--`:

```bash
bash run_alg_bench.sh -n 2 -p 2 --ccu-probes -- \
  --op allreduce -a ccu_sched -b 32K -e 32K
```

A matching SDK with KFC (Kernel Function Call) boundary probes is required. The launcher sets collection switches before each rank starts. Five columns report algorithm, queue-head, parameter-load/decision, completion-signal, and queue-tail intervals. The algorithm interval includes synchronization of CCU communication tasks (missions). These metrics do not replace Event, Wait, or bandwidth. Instrumentation affects execution timing; compare instrumented and default runs separately.

Each new metric requires complete coverage across all ranks and measured iterations; otherwise its cell shows `-`, with details in `profile_analysis/profiling_audit.json`. Missing probes do not invalidate existing metrics when the base capture is complete. This option cannot be combined with query-only mode, disabled profiling, or AICPU delayed submission, and does not change the operator package's measurement mode.

To validate device execution and output only, add `--no-profiling` before `--`. This mode reports whole-operator timing and leaves other phase metrics unavailable, so it cannot validate the full performance report.

If native query APIs are unavailable, add `--query-stub basic` after `--`. This simulates query candidates only; subsequent operators still execute on real NPUs. Stub candidates do not establish actual topology support. Rebuild normally when switching query sources.

## Read Performance Results

The terminal prints `Run directory:`, progress, and the performance table. Results default to `build_out/runs/run-UUID/`.

CCU benchmarks also print stage progress as `Benchmark phase:` and exit details as `Benchmark exit:`. Check the `status` exit code and report results to determine success; `phase=finished` means only that the workflow reached its end.

| File | Purpose |
| --- | --- |
| `report/report.csv` | Performance table with Chinese headers, encoded as UTF-8 with BOM for Excel/WPS |
| `report/summary.json` | Query source, measurement counts, and run failures |
| `report/diagnostics.json` | Per-entry states and error details |
| `profile_analysis/profiling_audit.json` | Profiling association and analysis checks |
| `collected/ranks/rank_*/stdout.log`, `stderr.log` | Original logs from each rank |

`Cluster PASS` confirms process completion only. Also verify `measured > 0`, populated applicable metrics, and successful analysis with default collection enabled. `NO MEASUREMENTS` means no performance values are available. `algorithm_unconfirmed` indicates real execution without evidence confirming the actual algorithm; measurements can still be available. Unmeasured or unavailable metrics show `-`; metrics for analysis failures show `PARSE_FAIL`.

Time metrics use microseconds. The main metrics are:

| Metric | Meaning |
| --- | --- |
| AICPU head, expansion, communication, tail, and kernel time | Device profiling phase boundaries; kernel time is the server execution interval |
| CCU communication time | Mean Wait duration across rank 0 measured iterations, as observed by the caller waiting for completion |
| CCU framework time | Mean whole-operator Event minus Wait for the same iterations; a residual outside Wait, not strictly pure framework cost |
| CCU kernel time | Rank 0 mean whole-operator Event duration; an Event is a device timing interval, and Write also includes the receive-result copy |
| CCU LoopGroup time | Sum of device communication instruction event durations, not algorithm-boundary instrumentation; a window with no complete events contributes zero, which does not prove zero communication |
| Five CCU probe intervals | Unavailable by default; populated after boundary collection with complete markers, excluding each boundary marker's own duration |
| Communication bandwidth | Computed from the actual test shape's bandwidth bytes and communication time, in GB/s |
| Kernel start skew p95/max | For each iteration, take the worst node's spread of rank start times, then compute p95 and max across measured iterations; not absolute cross-host clock skew |

## Troubleshoot Common Problems

| Symptom | Action |
| --- | --- |
| Query headers or APIs missing | Check that installed SDK headers and the query library match, and inspect `query_probe.log` in the backend build directory; updating source alone does not update the installed SDK |
| makeself unavailable | Check the selected CANN and PATH; for a nonstandard location, set `MC2_BENCH_MAKESELF` and retain the script's companion header |
| Operator package or backend build failure | Inspect the first error in the relevant `build.log`, then check the SDK, Host HCCL, and build dependencies |
| `--skip-build` verification failure | Remove the option and rebuild normally; old results need not be deleted |
| No query candidates or unmatched algorithm | Use `--query-only` to check topology, engine, and candidates; skipped entries do not constitute a performance pass |
| Empty metrics or analysis failure | Check whether `--no-profiling` was used, then inspect the audit and each rank's captures |
| CCU export failure | Check the SDK msprof; for a nonstandard location, add `--msprof /actual/path/msprof` before `--` |

Run `bash run_alg_bench.sh --help` for more launcher options.
