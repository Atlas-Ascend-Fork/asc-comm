# AscComm API Benchmark Multi-node Guide

English | [简体中文](../zh/multinode.md)

This example runs 16-rank AllReduce on two hosts, each with eight devices. A rank is a participating communication process. MPI starts processes across hosts, while HCCL transfers data over the device communication network. See the [Usage Guide](./usage.md) for single-host setup, parameters, and metrics.

## Prepare Both Hosts

Complete a single-host test first, then verify the following on both hosts:

- Compatible CANN, Host HCCL, MPI, and Python installations with their environments loaded
- Matching query headers, query libraries, and runtime dependencies, with the same absolute SDK, MPI, and run paths
- Noninteractive SSH access from the launch host to the other host, with SSH and rsync available
- Read and write access to result and operator package installation directories for the current user
- Available devices and configured, reachable host and device communication networks

Run the launcher only on the first host. The tool distributes the benchmark, deploys operator packages, and collects results. It does not install CANN/MPI or configure device networks.

The launcher reads `ASCEND_HOME_PATH`, `MPI_HOME`, and the optional `HCCL_DIR` on the launch host and sends the selected installation paths to all hosts. Installations must use the same absolute paths on every host.

## Create a Hostfile

Create the file in the tool directory. Replace `server-a` and `server-b` with actual hostnames or IP addresses; the first entry must be the launch host:

```bash
cat > hosts_2x8 <<'HOSTS'
server-a:8
server-b:8
HOSTS
```

Each line requests eight processes on that host. Every host's device count must match `-p 8`.

## Query Multi-node Candidates

Select the actual host network interface. Replace `eth0` below with an interface name appropriate for both hosts:

```bash
export HCCL_SOCKET_FAMILY=AF_INET
export HCCL_SOCKET_IFNAME=eth0
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 --query-only \
  --op allreduce -a aicpu_ts
```

Query-only mode creates a real communicator, prints candidates, and saves `query_summary.json`. Use `-a ccu_sched` for CCU queries. Candidates from one topology do not establish support for another.

## Run the Two-host Benchmark

Replace `sole[mesh]` with a complete algorithm name returned by the current multi-node query:

```bash
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 \
  --op allreduce -a aicpu_ts -b 32K -e 32K --alg 'sole[mesh]'
```

Use the same launcher for CCU:

```bash
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 \
  --op allreduce -a ccu_sched -b 32K -e 32K --alg 'sole[mesh]'
```

The default workflow builds, distributes, and installs the package for the selected engine, measures, and aggregates captures from both hosts.

Unmatched algorithm names are skipped. Omit `--alg` to test the candidate union. Do not directly apply this 16-rank example to AllToAllVWrite; check the installed SDK's support conditions first.

## Check Results

The terminal prints the run directory. First verify `Cluster PASS: 16/16 successful ranks`, then confirm populated metrics in `report/report.csv`, `measured > 0` and no run failures in `summary.json`, and a passing audit for default profiling analysis.

| Problem | First Checks |
| --- | --- |
| Remote processes not started or files not collected | SSH, rsync, Hostfile, directory permissions, and MPI environment |
| Communicator initialization failure or timeout | Host network interface selection, device network, HCCL configuration, and both SDK installations |
| Package deployment or loading failure on one host | Installation directory permissions, shared libraries, package mode, and build logs |
| Missing ranks or profiling analysis failure | `cluster_summary.json`, per-rank logs, and `profile_analysis/profiling_audit.json` |

On failure, retain the complete run directory and find the first error before retrying.
