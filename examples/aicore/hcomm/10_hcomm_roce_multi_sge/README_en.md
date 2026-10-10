# Hcomm RoCE Multi-SGE Batch Read/Write Example

## Overview

This example creates an `UbcBatchHandle` on a RoCE channel with `MakeBatchHandle`, then submits
scattered memory segments through the multi-SGE `WriteNbi` and `ReadNbi` overloads. MPI manages the
processes, exchanges HCCL root information, and synchronizes each phase.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
|---------|-----------------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## Example Behavior

- `WriteNbi` is called 128 times. Each call passes eight 64-byte scattered segments in `srcDescs` and
  writes them to one 512-byte contiguous remote `dst`.
- `ReadNbi` is called 128 times. Each call passes eight 64-byte scattered segments in `dstDescs` and
  reads from one 512-byte contiguous remote `src`.
- Every four `WriteNbi` calls are submitted with one `BatchCommit` and completed with one `Drain`.
  The 128 `ReadNbi` calls follow the same four-call commit-and-drain pattern.
- Rank 0 issues the writes and rank 1 verifies the remote data. Rank 1 then issues the reads and
  verifies its local scattered segments.

## Files

```text
10_hcomm_roce_multi_sge
├── CMakeLists.txt
├── README.md
├── README_en.md
├── roce_multi_sge.asc
├── roce_multi_sge_def.h
└── roce_multi_sge_kernel.cpp
```

## Data Layout

Each rank registers a 393216-byte communication buffer divided into four regions:

| Offset | Size | Purpose |
| --- | ---: | --- |
| `[0, 131072)` | 131072B | Local scattered source data: 128 calls × 8 segments, 64B each with 128B stride |
| `[131072, 196608)` | 65536B | Contiguous source data: 128 messages of 512B; initialized by rank 0 and read by rank 1 |
| `[196608, 262144)` | 65536B | Destination on rank 1 for the 128 contiguous 512B messages written by rank 0 |
| `[262144, 393216)` | 131072B | Local scattered result of `ReadNbi`: 64B segments with 128B stride |

The batch WQE buffer is `4 * 8 * 64` bytes in the kernel's VECOUT UB; one batch of WQEs is reused
every four calls.

## Build

As in sample 09, this sample directly uses the asc-comm headers installed in the CANN environment.
That installation must contain the RoCE batch multi-SGE interfaces.

```bash
cd examples/aicore/hcomm/10_hcomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## Enable MPI Environment Variables

Adjust the following settings according to the actual MPI installation path.

```bash
export MPI_HOME=/usr/local/mpi/mpich
export PATH="${MPI_HOME}/bin:${PATH}"
export LD_LIBRARY_PATH="${MPI_HOME}/lib:${LD_LIBRARY_PATH:-}"
```

## Run

Start the sample with two ranks:

```bash
cd examples/aicore/hcomm/10_hcomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
mpirun -n 2 $(pwd)/build/hcomm_roce_multi_sge
```

On success, the sample prints:

```text
[rank 0] hcomm_roce_multi_sge | WriteNbi=128 SGEs=8 | PASS
[rank 1] hcomm_roce_multi_sge | ReadNbi=128 SGEs=8 | PASS
RESULT | Example=hcomm_roce_multi_sge | Status=PASS
```

## Notes

- Running the sample requires at least two NPUs, and the local MPI rank count on each node must not exceed the available NPU count.
- Rank 0 and rank 1 must have a RoCE link, and the channel protocol must be COMM_PROTOCOL_ROCE.
- The CANN installation must contain the RoCE batch multi-SGE interfaces.
- Every four WriteNbi or ReadNbi calls are submitted and drained once, keeping the batch WQEs within SQ capacity.
- Local segment addresses and lengths are described by srcDescs/dstDescs; every segment must lie in registered communication memory.
