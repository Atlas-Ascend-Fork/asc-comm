# Asccomm RoCE Multi-SGE Read/Write Example

## Overview

This example submits scattered memory segments through the multi-SGE `write_nbi` and `read_nbi` overloads on a RoCE channel. It supports both batch and normal modes, selected by `--mode batch|normal`; batch is the default.
MPI manages the processes, exchanges HCCL root information, and synchronizes each phase.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Example Behavior

- In batch mode, `write_nbi` is called 128 times. Each call passes eight 64-byte scattered segments in
  `srcDescs` and writes them to one 512-byte contiguous remote `dst`.
- In batch mode, `read_nbi` is called 128 times. Each call passes eight 64-byte scattered segments in
  `dstDescs` and reads from one 512-byte contiguous remote `src`.
- In batch mode, all calls except the last `write_nbi` and the last `read_nbi` use the `cqe=0`
  configuration to suppress completion events. Those two calls use the default config template
  parameter, whose `cqe` is `1`, so the write path and read path each produce one CQE. The `poll_cq`
  inside `drain` handles only the corresponding completion event.
- In normal mode, `write_nbi` and `read_nbi` are each called once. The request is posted with
  `commit=false`, then published by one `commit`, and completed by one `drain`.
- In batch mode, all 128 `write_nbi` or `read_nbi` calls are added to the same batch handle, then
  submitted with one `batch_commit` and completed with one `drain`.
- Rank i communicates with rank i + N/2. In each pair, the lower rank issues the writes and the higher rank verifies the remote data. The higher rank then issues the reads and verifies its local scattered segments.

## Directory Structure

```text
09_asccomm_roce_multi_sge
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
| `[131072, 196608)` | 65536B | Contiguous source data: 128 messages of 512B; initialized by the lower rank and read by the higher rank in each pair |
| `[196608, 262144)` | 65536B | Destination on the higher rank for the 128 contiguous 512B messages written by the lower rank |
| `[262144, 393216)` | 131072B | Local scattered result of `read_nbi`: 64B segments with 128B stride |

The `128 * 3 * 64` byte VECOUT UB buffer is the contiguous WQE staging area in batch mode and the single-WQE construction area in normal mode.

## Build

As in sample 08, this sample directly uses the asc-comm headers installed in the CANN environment.
That installation must contain the RoCE batch multi-SGE interfaces.

```bash
cd examples/aicore/asccomm/09_asccomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
export MPI_HOME=/usr/local/mpich
mkdir build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

To enable `ASCENDC_DEBUG`, use `cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DASCENDC_DEBUG=ON ..`.

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
cd examples/aicore/asccomm/09_asccomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
mpirun -n 4 $(pwd)/build/asccomm_roce_multi_sge

Use the normal path:

mpirun -n 4 $(pwd)/build/asccomm_roce_multi_sge --mode normal

Select batch mode explicitly:

mpirun -n 4 $(pwd)/build/asccomm_roce_multi_sge --mode batch
```

In batch mode, the sample prints:

```text
[rank 1] asccomm_roce_multi_sge | peer=0 mode=batch write_nbi=128 SGEs=8 | PASS
[rank 1] asccomm_roce_multi_sge | peer=0 mode=batch read_nbi=128 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=batch write_nbi=128 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=batch read_nbi=128 SGEs=8 | PASS
RESULT | Example=asccomm_roce_multi_sge | Status=PASS
```

In normal mode, the sample prints:

```text
[rank 1] asccomm_roce_multi_sge | peer=0 mode=normal write_nbi=1 SGEs=8 | PASS
[rank 1] asccomm_roce_multi_sge | peer=0 mode=normal read_nbi=1 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=normal write_nbi=1 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=normal read_nbi=1 SGEs=8 | PASS
RESULT | Example=asccomm_roce_multi_sge | Status=PASS
```

## Notes

- Running the sample requires at least two NPUs; the rank count must be even and must not exceed the available NPU count.
- Every pair of rank i and rank i + N/2 must have a RoCE link, and the channel protocol must be COMM_PROTOCOL_ROCE.
- The CANN installation must contain the RoCE batch multi-SGE interfaces.
- In batch mode, all 128 write_nbi or read_nbi calls are submitted through one batch handle and then drained together.
- In normal mode, no batch handle is created; write_nbi or read_nbi is issued once, then committed and
  drained, and the first 512-byte message is verified.
- Local segment addresses and lengths are described by srcDescs/dstDescs; every segment must lie in registered communication memory.
