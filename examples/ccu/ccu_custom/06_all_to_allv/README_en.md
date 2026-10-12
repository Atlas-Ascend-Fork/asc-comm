# CCU AllToAllV Direct Run Sample

## Overview

This sample implements AllToAllV with an HCCL communication domain, CCU full-mesh channels, and `ccu::write`.
Each rank specifies variable-length data blocks per peer through `sendCounts/sendDispls` and `recvCounts/recvDispls`.

The CCU kernel publishes the output address with the receive offset already applied to each peer; remote data is sent with `ccu::write`, and local data with a single `ccu::local_copy`.

The sample supports FP32 only, non-in-place operation, at most 16 ranks, and a per-peer data block of at most 256 MiB.

## Build and Run

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

The test builds unequal-length data with `count(src, dst) = src + dst + 1` and prints `PASS` or `FAIL` on each rank after checking the output.
