# CCU AllToAllVC Direct Run Sample

## Overview

This sample implements AllToAllVC with an HCCL communication domain, CCU full-mesh channels, and `ccu::write`.
All ranks share one `rankSize * rankSize` count matrix; element `(src, dst)` specifies the number of elements src sends to dst.

The CCU kernel publishes the output address with the receive offset already applied to each peer; remote data is sent with `ccu::write`, and local data with a single `ccu::local_copy`.

The sample supports FP32 only, non-in-place operation, at most 16 ranks, and a per-peer data block of at most 256 MiB.

## Build and Run

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

The host computes the send layout from its row of the count matrix and the receive layout from its column. The test builds unequal-length data with
`count(src, dst) = src + dst + 1` and prints `PASS` or `FAIL`.
