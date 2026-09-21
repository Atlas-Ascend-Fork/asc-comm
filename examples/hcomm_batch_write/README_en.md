# Hcomm Shared-Jetty Batch Write Sample

## Overview

This sample demonstrates multiple `COMM_PROTOCOL_UBC_CTP` channels sharing one Jetty. Rank 0 groups peers by
local endpoint and creates one `MultiChannelHandle` for each group. The AIV kernel prepares `WriteNbi` requests for
the peers in a shared batch, submits them to the shared SQ with one `BatchCommit`, and calls `Drain` for completion.
Each receiving rank verifies the data written by rank 0.

The kernel uses the following flow:

```cpp
auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferSize);
for (uint32_t index = 0; index < channelNum; ++index) {
    GM_ADDR remoteAddr = remoteBuffers[index] + recvOffset;
    auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, index, remoteAddr);
    hcomm.WriteNbi(peerBatchHandle, remoteAddr, localBuffer, dataSize);
}
hcomm.BatchCommit(multiBatchHandle);
hcomm.Drain(multiBatchHandle);
```

`channelIndex` matches the index in the `channelDescs` array passed to Host-side `MakeMultiChannelHandle`.
`GetHandleRef` selects and caches an MR token using that index and the remote address, and returns the inner BatchHandle
reference. Use this reference for batch writes and the outer multi-channel batch handle for `BatchCommit` and `Drain`.

## Requirements

- Ascend 950PR or Ascend 950DT;
- at least two NPUs on one host;
- an asc-comm package containing the shared-Jetty Batch APIs installed in the active CANN environment.

With three or more NPUs, rank 0 can build a batch for multiple peers. The actual grouping depends on the local
endpoints reported by RankGraph.

## Build and Run

Set up the CANN environment first:

```bash
source ${install_path}/cann/set_env.sh
```

Build and run with two NPUs by default, or pass a rank count:

```bash
bash examples/hcomm_batch_write/run.sh
bash examples/hcomm_batch_write/run.sh 4
```

To build and run manually:

```bash
cmake -S examples/hcomm_batch_write -B build/examples/hcomm_batch_write \
    -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build/examples/hcomm_batch_write -j
./build/examples/hcomm_batch_write/hcomm_batch_write 4
```

A successful four-rank run ends with:

```text
hcomm 4-rank grouped batch write test passed
```
