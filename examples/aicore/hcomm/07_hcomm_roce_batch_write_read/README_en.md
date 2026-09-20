# Hcomm AIV RoCE Batched WriteNbi/ReadNbi Sample

## Overview

This sample uses MPI to manage multiple processes. Each MPI rank selects one NPU and validates the
batch interfaces provided by `hcomm_aiv_roce.h` over a `COMM_PROTOCOL_ROCE` channel:

- `MakeBatchHandle`
- `WriteNbi` with a `UbcBatchHandle` argument
- `ReadNbi` with a `UbcBatchHandle` argument
- `BatchCommit`
- `Drain`

Every rank connects to the next rank in a ring. One batch handle receives 1000 `WriteNbi` calls and
1000 `ReadNbi` calls. The `i`th call uses an address offset of `i * 64` bytes. After the batch is
committed and drained, each rank validates the 1000 slices written by the previous rank and the 1000
slices read back from the next rank.

## Build

Source the CANN environment and build from this directory:

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

CANN 9.1.0 or newer and an MPI C++ compiler (`mpicxx`) are required.

## Run

On a multi-NPU host, start one MPI process per NPU. Rank `i` uses NPU `i`:

```bash
cd examples/aicore/hcomm/07_hcomm_roce_batch_write_read
mpirun --allow-run-as-root -np 2 ./build/hcomm_roce_batch_write_read
```

The RoCE links between the NPUs must be available, and the MPI ranks must not exceed the number of
available NPUs. On success, rank 0 prints:

```text
RESULT | Example=hcomm_roce_batch_write_read | Status=PASS
```

## Buffer layout

Each rank registers a `3 * 1000 * 64` byte communication buffer:

| Region | Purpose |
| --- | --- |
| `[0, 64000)` | Source data owned by this rank |
| `[64000, 128000)` | Results written by the previous rank |
| `[128000, 192000)` | Results read from the next rank |

The batch WQE buffer lives in the Kernel VECOUT UB and contains `2000 * 64` bytes.
