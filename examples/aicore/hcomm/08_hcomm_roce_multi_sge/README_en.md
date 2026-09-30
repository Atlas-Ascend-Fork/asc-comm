# Hcomm RoCE Multi-SGE Batch Read/Write Example

This example creates an `UbcBatchHandle` on a RoCE channel with `MakeBatchHandle`, then submits
scattered memory segments through the multi-SGE `WriteNbi` and `ReadNbi` overloads. MPI manages the
processes, exchanges HCCL root information, and synchronizes each phase.

## Example Behavior

- `WriteNbi` is called 128 times. Each call passes eight 64-byte scattered segments in `srcDescs` and
  writes them to one 512-byte contiguous remote `dst`.
- `ReadNbi` is called 128 times. Each call passes eight 64-byte scattered segments in `dstDescs` and
  reads from one 512-byte contiguous remote `src`.
- Every four calls are submitted with `BatchCommit` and completed with `Drain`. This keeps the batch
  within SQ capacity and exercises the full batch-handle lifecycle.
- Rank 0 issues the writes and rank 1 verifies the remote data. Rank 1 then issues the reads and
  verifies its local scattered segments.

## Build and Run

As in sample 07, this sample directly uses the asc-comm headers installed in the CANN environment.
That installation must contain the RoCE batch multi-SGE interfaces.

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
mpirun -n 2 ./hcomm_roce_multi_sge
```

Rank 0 and rank 1 must have a RoCE link. On each node, the local MPI rank count must not exceed the
available NPU count.
