# AscComm API Benchmark

English | [简体中文](./README.md)

`asccomm_api_bench` queries MC2 communication algorithms for the current topology and benchmarks selected algorithms on NPUs (Neural Processing Units). MC2 coordinates communication and computation. The launcher builds and installs the operator package, compiles the benchmark, starts MPI processes, collects device profiling data, and generates reports.

## Supported Operations

Built-in benchmarks support multiple data types and default to `fp16` (16-bit floating-point), with a matching SDK for Ascend 950PR/Ascend 950DT devices. See the [usage guide](./docs/en/usage.md) for the types supported by each engine.

| Operation | `aicpu_ts` engine | `ccu_sched` engine |
| --- | --- | --- |
| `allgather`, `allreduce`, `reducescatter`, `alltoall`, `alltoallv` | Available for testing | Available for testing |
| `alltoallvwrite` | Unavailable | Available for testing |

Actual algorithm, reduction, and topology support depends on the installed SDK query results and execution.

## Quick Start

If you have not downloaded the source, retrieve it and enter the tool directory:

```bash
git clone https://gitcode.com/cann/asc-comm.git
cd asc-comm/tools/asccomm_api_bench
```

If you already have the source, enter `tools/asccomm_api_bench` directly. Then prepare and load CANN and MPI as described in the [Usage Guide](./docs/en/usage.md#configure-the-environment). The tool does not install these dependencies.

Once the environment is ready, run AICPU AllReduce on two devices with 32KiB of data for the first test. MPI starts two participating communication processes, called ranks.

```bash
bash run_alg_bench.sh -n 2 -p 2 \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

This command queries candidates for 32KiB and benchmarks each one automatically. No separate query or algorithm name is required. The tool handles building, installation, execution, and reporting.

## Results

The terminal prints `Run directory:` and the performance table. Each invocation creates a directory under `build_out/runs/run-UUID/` by default.

- Benchmark: `report/report.csv` and `report/summary.json`
- Failure: Keep the complete run directory and inspect the indicated build or rank logs

Confirm that a performance table is shown, `measured > 0`, applicable metrics are populated, and analysis succeeds. `Cluster PASS` confirms process completion only. For `NO MEASUREMENTS`, check the query results and SDK support as described in the usage guide. The terminal performance table and CSV use Chinese column names.

## Select an Algorithm or Change Engines

To list candidates for the same conditions without benchmarking:

```bash
bash run_alg_bench.sh -n 2 -p 2 --query-only \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

This mode saves candidates to `query_summary.json` and does not generate a performance CSV.

To select an algorithm, append `--alg 'complete-candidate-name'` to the benchmark command. Copy the name from query results for the same operation and engine. Unmatched names are skipped. See the [Usage Guide](./docs/en/usage.md#query-and-select-algorithms) for a complete example.

For CCU, replace `-a aicpu_ts` with `-a ccu_sched`. The launcher prepares the package for that engine and runs the benchmark.

## Documentation

- [Usage Guide](./docs/en/usage.md): Environment, parameters, queries, metrics, and troubleshooting
- [Multi-node Guide](./docs/en/multinode.md): Preparation and execution on two eight-device hosts

## Contributions and License

Follow the [contribution guide](../../CONTRIBUTING_en.md) and [documentation contribution guide](../../docs/en/contributing/doc_contributing.md). The tool uses the root [CANN license](../../LICENSE). External tools retain their own licenses.
