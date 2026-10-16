# CCU AllGather Mesh1D Mem2Mem

This sample registers and launches a CCU kernel directly from the host, demonstrating the Mesh1D Mem2Mem
AllGather data plane.

## Overview

```text
Python generates input and golden data
  -> initialize the HCCL communication domain
  -> acquire CCU threads and UBC_CTP channels
  -> register the CCU kernel
  -> exchange output addresses and tokens
  -> write the local rank's slice to all peers
  -> GroupCopy the local slice
  -> wait for all transfers to complete
  -> C++ writes out the result of each rank
  -> Python verifies the output of each rank
```

Channels are acquired in one batch through a single `HcclChannelAcquire` call. Data transfer uses events grouped
by rank: every 16 ranks share one event, and each rank occupies one event bit.

The host and kernel share a unified 10-argument task ABI, keeping the per-round transfer size `chunkSize` separate
from the rank stride `outputSliceStride`, which preserves the address layout for future host-side multi-round chunking.

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
cd examples/ccu/ccu_custom/02_allgather_mesh1d_mem2mem
```

Generate input and golden data:

```bash
python3 scripts/gen_data.py --rank-size 2 --count 256
```

Build and run the operator:

```bash
env DEVICES=0,1 COUNT=256 ./run.sh
```

Verify the output of each rank:

```bash
python3 scripts/verify_result.py --rank-size 2 --count 256
```

The data generation and verification scripts require Python 3 and NumPy. `--rank-size` must match the number of
devices specified by `DEVICES`, and `--count` must match the `COUNT` used when running the operator.

`run.sh` only builds and runs the operator; it does not generate or verify data automatically. To build only:

```bash
env BUILD_ONLY=1 ./run.sh
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

After changing the rank count or `count`, regenerate the data and verify with the same parameters.
For example, with 4 ranks and 256 input elements per rank, run:

```bash
python3 scripts/gen_data.py --rank-size 4 --count 256
env DEVICES=0,1,2,3 COUNT=256 ./run.sh
python3 scripts/verify_result.py --rank-size 4 --count 256
```

## Sample Result

The following is partial output with 2 ranks and 256 `float` elements per rank. Print order across
ranks may vary with thread scheduling.

```text
generated 2 rank inputs and golden data, 256 FP32 elements per rank
rank 1: launched, waiting for stream
rank 0: launched, waiting for stream
rank 0: stream completed
rank 1: stream completed
rank 1 input : [ 1000001 1000002 1000003 1000004 1000005 1000006 1000007 1000008 1000009 1000010 1000011 1000012 1000013 1000014 1000015 1000016 ...(256) ]
rank 1 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
rank 1: output saved to <sample_path>/output/output_rank_1.bin
rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
rank 0: output saved to <sample_path>/output/output_rank_0.bin
rank 0 verify pass
rank 1 verify pass
test pass!
```
