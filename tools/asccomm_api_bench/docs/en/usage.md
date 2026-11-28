# AscComm API Benchmark Usage Guide

English | [简体中文](../zh/usage.md)

This guide covers routine use of the built-in backend. See [Supported Operations](../../README_en.md#supported-operations) for operations and engines, and the [Multi-node Guide](./multinode.md) for execution on two hosts.

## Configure the Environment

Prepare a matching CANN SDK, Host HCCL (the host-side collective communication library), an MPICH-compatible MPI environment, Python 3.9+, CMake 3.19+, and a C++11 compiler. Devices must be available, and the current user must have read and write access to the operator package installation directory.

The CANN SDK must provide `Mc2GetSupportAlgList`, `Mc2SetCcDataLenRange`, and a matching `libmc2_client.so`. Default profiling (device performance data collection) requires SDK ACL collection APIs and `libmsprofiler.so`. CCU raw data export also requires a matching SDK `msprof`. AICPU analyzes raw captures directly without exporting a timeline.

Set the installation paths in Bash and enter the tool directory. Replace the CANN, MPI, and source paths below with actual locations. Existing dependencies do not need to be reinstalled:

```bash
export ASCEND_HOME_PATH=/usr/local/Ascend/cann
export MPI_HOME=/usr/local/mpi/mpich
cd /path/to/asc-comm/tools/asccomm_api_bench
```

The tool loads the selected CANN environment during builds and device process startup, and adds the MPI executable and shared-library search paths. Variable names are case-sensitive. Existing correct environment settings can be reused.

Host HCCL is the host-side collective communication library. By default, the tool uses headers and libraries under the selected CANN directory. Only for a separate Host HCCL installation, set its installation root:

```bash
export HCCL_DIR=/actual/HostHCCL/installation
```

The build first looks for the makeself packaging tool in CANN, then on the system PATH. A separate setting is usually unnecessary. With a standard installation layout, the CCU dependency `libhcomm.so` is linked automatically.

For the first run, go directly to [Run a Single-host Benchmark](#run-a-single-host-benchmark), use the AICPU example, then [Read Performance Results](#read-performance-results). Consult the parameter and query sections as needed.

## Place Command Arguments

Write all options in one command without a separator. Options may be reordered, but each value must follow its option.

```text
bash run_alg_bench.sh -n 8 -p 8 --op allreduce -a ccu_sched -b 1M -e 1M
```

| Argument | Meaning and Default |
| --- | --- |
| `-n`, `-p` | Total ranks and devices per node, both defaulting to 8; a rank is a participating communication process |
| `--query-only` | List candidates without running benchmarks |
| `--output-dir` | New result directory, allocated automatically when omitted; existing jobs cannot be overwritten |
| `--op` | Required operation name |
| `-a` | Engine: `aicpu_ts` or `ccu_sched`, defaulting to `aicpu_ts` |
| `-d` | Data type, defaulting to `fp16` (16-bit floating-point); supported types are listed below |
| `-o` | Reduction: `sum`, `prod`, `max`, or `min`, defaulting to `sum`; actual support depends on the SDK |
| `-b`, `-e` | Starting and target ending byte lengths, defaulting to 32KiB and 256MiB |
| `-f` or `-i` | Multiplicative factor or linear byte step, mutually exclusive; the default is `-f 2` |
| `--alg` | Complete algorithm name returned by the query; repeatable, or omit to test all candidates |

Measurement data types depend on the engine. Specify one type per command:

| Engine | Supported data types |
| --- | --- |
| `aicpu_ts` | `int8`, `int64`, `uint64`, `fp16`, `fp32`, `fp64`, `bf16` |
| `ccu_sched` | `int8`, `fp16`, `fp32`, `bf16` |

`bfp16` is an alias for `bf16`; reports use `bf16`. When `-d` or `-o` is omitted, both queries and measurements use the SDK defaults of `fp16` or `sum`, respectively. Explicit values override the defaults. For example, add `-d fp32 -o max` to query and measure with FP32 and MAX. The installed SDK must support the selected operator, algorithm, data type, and reduction combination. Query-only mode accepts a wider set of SDK data types.

Lengths accept suffixes such as `B`, `K`, `KB`, `KiB`, `M`, `MB`, and `MiB`, using powers of 1024. Set both `-b` and `-e` for a single point. If only one endpoint is set, the other keeps its default. Multiplicative scans include the first point at or above the target ending length; linear scans stop at or below it. The default range contains 14 points.

Requested bytes may differ from each rank's input and output sizes: AllGather uses the total received size, ReduceScatter uses the total input size, and AllToAll operations use the total sent size per rank. The backend checks divisibility by rank count and data type. AllToAllV uses evenly split data.

## Query and Select Algorithms

This step is optional. To inspect candidates before selecting an algorithm, use the same AICPU engine, two devices, and 32KiB point as the first benchmark:

```bash
bash run_alg_bench.sh -n 2 -p 2 --query-only \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

The command filters by the 32KiB point. Omit `-b/-e` to query all candidates for the current conditions, or use `-b 32K -e 128K` for a range. The query length is called `dataLen`; the rules are:

| User Input | Query Behavior |
| --- | --- |
| `--alg` specified, with or without data lengths | Query once with `dataLen=0` and check whether the requested names match |
| No algorithms or lengths specified | Query all candidates for the current conditions with `dataLen=0` |
| No algorithms specified, with a single point | Pass that point's byte length to the query |
| No algorithms specified, with a range | Query each expanded point and take the deduplicated union of algorithm names |

Query conditions also include the data type and reduction. `dataLen=0` skips only length filtering; it does not query every data type and reduction combination.

Query-only mode still requires real devices and a communicator. It does not build operator packages, warm up, measure, or generate a performance CSV. Results are printed and saved to `query_summary.json` in the run directory.

The algorithm selection summary places requested names, candidates, and matches together for comparison.

A benchmark queries again and measures only matched candidates. Unmatched names are reported as unsupported and skipped, without a separate tool-side fallback. Range mode tests the candidate union across the complete measurement length sequence. A returned candidate does not guarantee successful execution at every length.

To select an algorithm, copy its complete name from query results for the same operation and engine. For example, use the following only if the preceding AICPU query returns `sole[mesh]`:

```bash
bash run_alg_bench.sh -n 2 -p 2 \
  --op allreduce -a aicpu_ts -b 32K -e 32K --alg 'sole[mesh]'
```

## Run a Single-host Benchmark

The following commands use two devices and a 32KiB point. No algorithm is specified: the tool queries and benchmarks candidates for those conditions automatically. A separate query-only command is unnecessary.

AICPU AllReduce:

```bash
bash run_alg_bench.sh -n 2 -p 2 \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

CCU AllReduce:

```bash
bash run_alg_bench.sh -n 2 -p 2 \
  --op allreduce -a ccu_sched -b 32K -e 32K
```

CCU AllToAllVWrite:

```bash
bash run_alg_bench.sh -n 2 -p 2 \
  --op alltoallvwrite -a ccu_sched -b 32K -e 32K
```

AllToAllVWrite supports the CCU data types listed above. Input, output, and query types follow `-d`, which defaults to `fp16`. The element size determines transfer sizes and the communication-window layout. The SDK provides the communication window, whose capacity is checked. Validate on one host first; do not directly apply the two-host, 16-rank example. Actual support depends on the SDK query.

For other standard operations, replace `--op`. The launcher prepares the engine's operator package, compiles the benchmark, queries, warms up, measures, validates, and generates reports automatically. No separate build or analysis command is required. Each test entry validates its last measured output after the measured iterations finish.

Each invocation creates a new result directory; previous results need not be deleted.

## Read Performance Results

The terminal prints `Run directory:`, progress, and the performance table. Results default to `build_out/runs/run-UUID/`.

CCU benchmarks also print stage progress as `Benchmark phase:` and exit details as `Benchmark exit:`. Check the `status` exit code and report results to determine success; `phase=finished` means only that the workflow reached its end.

| File | Purpose |
| --- | --- |
| `report/report.csv` | Performance table with Chinese headers, encoded as UTF-8 with BOM for Excel/WPS |
| `report/summary.json` | Query source, measurement counts, and run failures |
| `report/diagnostics.json` | Per-entry states and error details |
| `report/ccu_probe_retries.json` | CCU probe recollection attempts and selected data sources, when needed |
| `profile_analysis/profiling_audit.json` | Profiling association and analysis checks |
| `collected/ranks/rank_*/stdout.log`, `stderr.log` | Original logs from each rank |

`Cluster PASS` confirms process completion only. Also verify `measured > 0`, populated applicable metrics, and successful analysis with default collection enabled. `NO MEASUREMENTS` means no performance values are available. Unmeasured or unavailable metrics show `-`; inapplicable metrics show `NA`; metrics for analysis failures show `PARSE_FAIL`.

`api算法` shows the public name of the algorithm actually executed, such as `sole[mesh]`. A request for A that falls back to B displays B's public name. Missing execution evidence, an unresolved name mapping, or conflicting rank records leaves the cell blank while preserving available performance values. Use a complete name returned by the query for `--alg`. Requested configurations, internal executor names, and mapping evidence remain in `report/diagnostics.json`.

Algorithm context evidence follows each rank process within the current run. Later entries without creation logs inherit evidence only while the requested configuration stays unchanged and one compatible executor remains. When different executors are cached, explicit context-get or cache-hit logs must identify the reused resource. Context tags and source logs remain in `report/diagnostics.json`.

Successfully executed CCU entries with zero complete probe samples or missing five-stage metrics are automatically rerun at the original size and requested algorithm, for up to three attempts. Verified build and installation artifacts are reused; execution and profiling collection run again. The first valid attempt supplies the entry's entire set of performance values. Entries that already have valid probe data retain their results. Original captures and attempts under `retries/` are preserved; `report/selected_profiling_audit.json` identifies the final report's sources. Unrecovered entries retain missing data and the run reports failure.

Multi-algorithm runs print a comparison after the performance table, selecting the lowest valid `api通信耗时(us)` at each size, including ties. Its columns show data size, executed algorithm (`执行算法`), communication time, and valid/planned entries. Executed algorithms use their public names. Missing, failed, or unidentified results are excluded. A single valid executor or conflicting timings for duplicate executors prevents an automatic comparison. Requested configurations remain in JSON. No CSV columns are added.

Time metrics use microseconds. The main metrics are:

| Metric | Meaning |
| --- | --- |
| AICPU head, communication, and tail time | Device profiling phase boundaries |
| CCU communication time | Mean Wait duration across rank 0 measured iterations, as observed by the caller waiting for completion |
| CCU framework time | Mean whole-operator Event minus Wait for the same iterations; a residual outside Wait, not strictly pure framework cost |
| CCU LoopGroup time | Sum of device communication instruction event durations; a window with no complete events contributes zero, which does not prove zero communication |
| Communication bandwidth | Computed from the actual test shape's bandwidth bytes and communication time, in GB/s |
| Kernel start skew p95/max | For each iteration, take the worst node's spread of rank start times, then compute p95 and max across measured iterations; not absolute cross-host clock skew |

CSV reports have 13 columns for AICPU and 16 for CCU. Both CSV and terminal performance tables omit the status column. Per-entry states remain in `report/diagnostics.json`, and the terminal still prints the overall outcome and error reasons. Full terminal performance tables additionally include the engine, so their column counts differ from CSV. CCU reports include algorithm and KFC stage timings, with all stage means using the same complete samples. Whole-operator Event measurements remain in JSON diagnostics and are not displayed as a separate CCU performance column. Coverage is recorded in `profile_analysis/profiling_audit.json` and is not displayed as CSV columns. No complete samples leaves the stage timings as `-` and fails the run while preserving available base measurements.

## Troubleshoot Common Problems

| Symptom | Action |
| --- | --- |
| Query headers or APIs missing | Check that installed SDK headers and the query library match, and inspect `query_probe.log` in the backend build directory; updating source alone does not update the installed SDK |
| makeself unavailable | Check that the selected CANN installation is complete and PATH is correct |
| Operator package or backend build failure | Inspect the first error in the relevant `build.log`, then check the SDK, Host HCCL, and build dependencies |
| No query candidates or unmatched algorithm | Use `--query-only` to check topology, engine, and candidates; skipped entries do not constitute a performance pass |
| Empty metrics or analysis failure | Inspect the audit and each rank's captures for missing or incomplete data |
| CCU export failure | Check that the SDK msprof and its dependencies match |

Run `bash run_alg_bench.sh --help` to view command arguments.
