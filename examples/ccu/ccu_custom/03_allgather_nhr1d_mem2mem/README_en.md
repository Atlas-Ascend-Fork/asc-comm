# CCU AllGather NHR1D Mem2Mem

This sample registers and launches a CCU kernel directly from the host, demonstrating the NHR1D Mem2Mem
AllGather data plane.

## Overview

```text
Python generates input and golden data
  -> initialize the HCCL communication domain
  -> generate NHR steps and the rank-to-channel mapping
  -> acquire CCU channels in one batch
  -> register and launch the CCU kernel
  -> GroupCopy the local slice
  -> write and forward slices remotely per NHR step
  -> C++ writes out the result of each rank
  -> Python verifies the output of each rank
```

NHR uses `ceil(log2(rankSize))` steps. The peers of step `step` are:

```text
delta    = 1 << (stepNum - 1 - step)
toRank   = (rank + delta) % rankSize
fromRank = (rank + rankSize - delta) % rankSize
```

For example, with 4 ranks:

```text
step 0:
  rank0 -> rank2: slice0
  rank1 -> rank3: slice1
  rank2 -> rank0: slice2
  rank3 -> rank1: slice3

step 1:
  rank0 -> rank1: slice0,slice2
  rank1 -> rank2: slice1,slice3
  rank2 -> rank3: slice2,slice0
  rank3 -> rank0: slice3,slice1
```

After each non-final step, peers synchronize to ensure the data to be forwarded in the next step has already been written to the local output.

The host and kernel share a unified 10-argument task ABI, keeping the per-round transfer size `chunkSize`
separate from the rank stride `outputSliceStride`, which preserves the address layout for future host-side multi-round chunking.

## Supported Scope

- Compilation architecture: `dav-3510`
- CANN version: 9.1 or later
- Data type: `float`
- Communication scope: single die, single mission, 1 to 16 ranks
- Data layout: out-of-place AllGather, equal slice size on every rank

## Build and Run

Set the CANN runtime environment and CCU scheduling mode before running:

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
cd examples/ccu/ccu_custom/03_allgather_nhr1d_mem2mem
```

Generate input and golden data:

```bash
python3 scripts/gen_data.py --rank-size 4 --count 256
```

Build and run the operator:

```bash
env DEVICES=0,1,2,3 COUNT=256 ./run.sh
```

Verify the output of each rank:

```bash
python3 scripts/verify_result.py --rank-size 4 --count 256
```

## Input and Verification

`scripts/gen_data.py` generates one FP32 input file per rank:

```text
input/input_rank_<rank_id>.bin
```

The input of each rank is:

```text
input[i] = rank * 1000000 + i + 1
```

Golden data is concatenated in source-rank order and written to `output/golden.bin`. The C++ program writes the
result of each rank to `output/output_rank_<rank_id>.bin`, which
`scripts/verify_result.py` then verifies element by element.

The data generation and verification scripts require Python 3 and NumPy. `--rank-size` must match the number of
devices specified by `--devices`, and `--count` must match the value used when running the operator.

`run.sh` does not generate or verify data automatically. After changing the rank count or `count`,
regenerate the input and golden data.

## Sample Result

The following is sample output with 4 ranks and 256 `float` elements per rank. The 4-card case contains
2 communication steps, which shows how NHR1D forwards already-received data in the later step. Print order across
ranks may vary with thread scheduling.

```text
rank 2 NHR schedule: step0(to=0,from=0,tx=[2]) step1(to=3,from=1,tx=[2,0])
rank 1 NHR schedule: step0(to=3,from=3,tx=[1]) step1(to=2,from=0,tx=[1,3])
rank 3 NHR schedule: step0(to=1,from=1,tx=[3]) step1(to=0,from=2,tx=[3,1])
rank 0 NHR schedule: step0(to=2,from=2,tx=[0]) step1(to=1,from=3,tx=[0,2])
rank 2: launched, waiting for stream
rank 1: launched, waiting for stream
rank 3: launched, waiting for stream
rank 0: launched, waiting for stream
rank 0: stream completed
rank 1: stream completed
rank 2: stream completed
rank 3: stream completed
rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(1024) ]
rank 0: output saved to <sample_path>/output/output_rank_0.bin
rank 1: output saved to <sample_path>/output/output_rank_1.bin
rank 2: output saved to <sample_path>/output/output_rank_2.bin
rank 3: output saved to <sample_path>/output/output_rank_3.bin
rank 0 verify pass
rank 1 verify pass
rank 2 verify pass
rank 3 verify pass
test pass!
```
