# Asccomm AIV RoCE Atomic/Inline MPI Sample

## Overview

This sample calls the ordinary Asccomm RoCE interfaces directly in an Ascend C AIV kernel, covering `atomic_faa_nbi`, `atomic_cas_nbi`, `write_value_nbi`, `init`, `commit`, and `drain`. MPI manages the host processes. Each process initializes an HCCL communicator, registers communication memory, creates a RoCE P2P channel, and validates the result after the kernel completes.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
|---------|-----------------------|
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Files

```text
10_asccomm_roce_atomic_inline
├── CMakeLists.txt
├── README.md
├── README_en.md
├── roce_atomic_inline.asc
├── roce_atomic_inline_def.h
└── roce_atomic_inline_kernel.cpp
```

## Behavior

Rank i communicates with rank i + N/2. Each rank creates only its channel to the unique peer.

The kernel calls the interfaces in this order:

```cpp
asccomm.init(...);                                  // Initialize the Asccomm UB workspace
asccomm.atomic_faa_nbi<uint64_t, false>(...);      // Post a deferred remote fetch-and-add
asccomm.atomic_cas_nbi<uint64_t, false>(...);      // Post a deferred remote compare-and-swap
asccomm.write_value_nbi<uint64_t, false>(...);    // Post a deferred inline scalar write
asccomm.commit<PIPE_S>(channel);                   // Trigger the doorbell once
asccomm.drain<PIPE_MTE3>(channel);                // Wait for CQE completion
```

## Data Layout

Each rank registers a 4096-byte communication buffer. The first 40 bytes are used:

| Offset | Initial | Expected after operation | Purpose |
| --- | ---: | ---: | --- |
| 0 | 0 | 100 | Remote result written by the peer's `atomic_faa_nbi` |
| 8 | any | any | Old value fetched into this rank by the peer's `atomic_faa_nbi` |
| 16 | 200 | 300 | Remote result written by the peer's `atomic_cas_nbi` |
| 24 | any | any | Old value fetched into this rank by the peer's `atomic_cas_nbi` |
| 32 | any | 400 | Remote result written by the peer's `write_value_nbi` |

After the kernel, the host validates offsets 0, 16, and 32 in the local communication buffer.

## Build

```bash
cd examples/aicore/asccomm/10_asccomm_roce_atomic_inline
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
cd examples/aicore/asccomm/10_asccomm_roce_atomic_inline
source ${install_path}/cann/set_env.sh
mpirun -n 4 $(pwd)/build/asccomm_roce_atomic_inline
```

A successful run prints:

```text
[rank 0] asccomm_roce_atomic_inline | peer=2 | PASS
[rank 1] asccomm_roce_atomic_inline | peer=3 | PASS
[rank 2] asccomm_roce_atomic_inline | peer=0 | PASS
[rank 3] asccomm_roce_atomic_inline | peer=1 | PASS
RESULT | Example=asccomm_roce_atomic_inline | Status=PASS
```

## Notes

- Running the sample requires at least two NPUs; the rank count must be even and must not exceed the available NPU count.
- The channel must use COMM_PROTOCOL_ROCE, matching `asc::asccomm<asc::COMM_PROTOCOL_ROCE>` on the kernel side.
- All three data-plane interfaces use `commit=false`, so `commit` is required. Data is validated only after `drain` returns.
- This sample validates the remote results modified by the peer; it does not validate the fetch buffers.
