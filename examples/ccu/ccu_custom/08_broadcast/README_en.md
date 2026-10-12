# CCU Broadcast Direct Run Sample

## Overview

This sample implements Broadcast with an HCCL communication domain, CCU full-mesh channels, and `ccu::write`.
All ranks first exchange local buffer addresses and tokens, and then the root rank writes the complete data directly to the other ranks.

This implementation targets flow verification and does not use the scatter+allgather optimization for large data in production HCCL.

The sample supports FP32 only, at most 16 ranks, and a single message of at most 256 MiB.

## Build and Run

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

The test uses root rank 0 with a message of `rankSize` elements and prints `PASS` or `FAIL` on all ranks.
