# Hcomm Shared-Jetty Batch Write Single-Host Multi-Device Sample

This sample validates AIV `WriteNbi` WQEs targeting different peers, grouped by local
`EndpointDesc.commAddr.raws`, and submitted to each group's shared SQ by one `BatchCommit`.

The optional `rank_size` argument selects the number of local ranks and NPUs; it defaults to two. Rank 0 selects a
`COMM_PROTOCOL_UBC_CTP` link to ranks 1 through N-1 and groups links with identical local `commAddr.raws`. Other
ranks only create a link to rank 0. Each group receives one `MultiChannelHandle` and a distinct `sharedQueueTag`.
The Host helper stores one shared `SqContext/CqContext` pair and one remote record per channel, preserving the input
channel order. Each record references the channel's remote MR table without duplicating its entries. Batch
`WriteNbi` resolves the token by finding `dst` in that table.

Prerequisites:

- Ascend 950PR or Ascend 950DT with at least two NPUs;
- an HCOMM package containing the shared-Jetty `HcclChannelAcquireWithConfig` control plane;
- the development run-package logic from `origin/run` merged with this Batch implementation and the resulting
  package installed into the active CANN;
- a sourced CANN environment with `ASCEND_HOME_PATH` pointing to that installation.

The Host helper is header-only. The sample consumes both its Host header and its AICore headers directly from the
active CANN installation and does not build an additional Host shared library.

Build and run:

```bash
cmake -S examples/hcomm_batch_write -B build/examples/hcomm_batch_write
cmake --build build/examples/hcomm_batch_write -j
./build/examples/hcomm_batch_write/hcomm_batch_write
./build/examples/hcomm_batch_write/hcomm_batch_write 4
```

The executable forks one process per local rank and maps rank IDs directly to device IDs. After all channels are
ready, only rank 0 runs the grouped Batch kernels and writes to all other ranks. The receivers verify rank 0's data
after `Drain` and a second barrier. Cross-host execution is not supported. A successful four-device run ends with
`hcomm 4-rank grouped batch write test passed`.
