# Asccomm AIV RoCE Batch API Performance Example

## Overview

This example is adapted from 09_asccomm_roce_batch_write_read to measure the RoCE batch APIs at different payload sizes. By default, it runs the complete write_nbi flow with payloads starting at 8 bytes and doubling up to 16 MB. Use --api read to run the complete read_nbi flow.
Requests in one batch do not overlap: request i uses an offset of i * AlignUp(dataSize, 64) bytes.

By default, `--mode batch` measures the batch APIs. Use `--mode normal` to measure the normal APIs. Both modes run 100 warmup rounds and average the following 100 rounds.

Each batch-mode round contains these phases:

1. make_batch_handle
2. write_nbi or read_nbi 1024 times by default, configurable with --batch-size; the first N-1 requests use cqe=0 and the Nth uses cqe=1
3. batch_commit
4. drain

The Nth request generates the only CQE in the batch, so drain waits for all requests in that batch.

Each normal-mode round contains these phases:

1. init
2. write_nbi or read_nbi: the first 999 requests use commit=false and cqe=0, and the 1000th uses commit=true with cqe=1
3. The first 999 requests and the 1000th commit=true request are timed as one whole phase; commit is not timed separately
4. drain

Batch mode records issue time per request, the per-request average of all batch requests plus one commit, batch_commit, drain, and total elapsed time used for throughput calculation. Normal mode records only the per-request average of all requests plus the final commit, drain, and throughput. All times are in microseconds (us), using 1 cycle = 1 ns.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
|---------|-----------------------|
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Files

```text
11_asccomm_roce_api_perf
├── CMakeLists.txt
├── roce_api_perf.asc // MPI, ACL, HCCL channel setup, timing, and output
├── roce_api_perf_kernel.cpp // AIV kernel batch API calls and phase timing
├── roce_api_perf_def.h // Constants and context shared by host and kernel
├── parse_perf_data.py // Parse run output and generate a table
├── README.md
└── README_en.md
```

## Data Layout

At startup, the example calculates, allocates, and registers one contiguous communication memory region from --max-bytes and --batch-size; the registered region is at least 4 KB. --min-bytes and --max-bytes are configurable within the range from 8 bytes to 16 MB, and the registered memory size adapts to --max-bytes. The send buffer starts at the region base, and the receive buffer starts after the 64-byte-aligned request span. Requests use payload-sized slots aligned up to 64 bytes based on the initial registered address; no request registers a new memory region.
The total registered size is max(4096, 2 * batchSize * AlignUp(maxBytes, 64)). The first half is the send region and the second half is the receive region. Both regions contain batchSize request slots of AlignUp(maxBytes, 64) bytes each. For write_nbi, a local send slot is the source and the peer receive slot is the destination. For read_nbi, a local receive slot is the destination and the peer send slot is the source. Requests in one batch advance by AlignUp(dataSize, 64) bytes based on the initial registered address; no request registers a new memory region.
The send region is filled with a data pattern that varies by rank and address offset, which detects requests landing in the wrong slot or data being shifted.

## Build

```bash
cd examples/aicore/asccomm/11_asccomm_roce_api_perf
source `CANN install path`/set_env.sh
export MPI_HOME=/usr/local/mpich
mkdir build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

To enable `ASCENDC_DEBUG`, use `cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DASCENDC_DEBUG=ON ..`.

An MPI C++ compiler (mpicxx) is required.

The RoCE backend is selected with the `ASCCOMM_ROCE_BACKEND_MLX5` CMake option: when the option is disabled, the default 1825 NIC is used; when the option is enabled, the mlx5 NIC is used. The option is enabled by default and can be set explicitly with `-DASCCOMM_ROCE_BACKEND_MLX5=OFF` or `-DASCCOMM_ROCE_BACKEND_MLX5=ON`.

## Enable MPI Environment Variables

Adjust the following settings according to the actual MPI installation path.

```bash
export MPI_HOME=/usr/local/mpich
export PATH="${MPI_HOME}/bin:${PATH}"
export LD_LIBRARY_PATH="${MPI_HOME}/lib:${LD_LIBRARY_PATH:-}"
```

## Run

```bash
cd examples/aicore/asccomm/11_asccomm_roce_api_perf
source `CANN install path`/set_env.sh
mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf 2>&1 | tee run.log
```

Optional arguments:

```bash
mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf \
  --mode batch --api write --batch-size 1024 --min-bytes 8 --max-bytes 16777216 2>&1 | tee run.log
```

Run the batch mode:

```bash
mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf \
  --mode batch --api write --batch-size 1024 --min-bytes 8 --max-bytes 16777216 2>&1 | tee run.log
```

Run the normal mode:

```bash
mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf \
  --mode normal --api write --min-bytes 8 --max-bytes 16777216 2>&1 | tee run.log
```

The output contains one PERF_DATA record per rank, API, and payload size:

```text
    Rank=0 | Mode=batch | API=write_nbi | DataBytes=256 | BatchSize=1024 | IssuePerRequest(Us)=... | IssueCommitPerRequest(Us)=... | batch_commit(Us)=... | drain(Us)=... | DeviceTime(Us)=... | DeviceGBps=...
    Rank=0 | Mode=normal | API=write_nbi | DataBytes=256 | LoopSize=1000 | IssueCommitPerRequest(Us)=... | drain(Us)=... | DeviceTime(Us)=... | DeviceGBps=...
RESULT | Example=asccomm_roce_api_perf | Status=PASS
```

Valid values for --api are write and read; the default is write. Valid values for --mode are batch and normal; the default is batch.
--batch-size controls the request count of each batch-mode round only; normal mode always runs 1000 requests.

## Export Results

Convert the run log to a CSV table:

```bash
python3 parse_perf_data.py --csv run.log
```

This creates perf_data.csv in the current directory by default.

## Notes

- At least two NPUs are required, and the rank count must not exceed the available NPU count.
- The channel must use COMM_PROTOCOL_ROCE, matching `asc::asccomm<asc::COMM_PROTOCOL_ROCE>` on the kernel side.
- MPI Bcast distributes the root info generated by HcclGetRootInfo to all ranks.
- A full default run registers 32 GB of contiguous device memory on each rank (16 GB for send and 16 GB for receive). When --max-bytes specifies a smaller upper bound, the registered memory shrinks accordingly.
- All communication addresses must be 64-byte aligned.
- After both write_nbi and read_nbi complete, the first min(dataSize, 256) bytes of each request slot are validated. Local receive slots are cleared before read_nbi to prevent stale data from affecting validation.
