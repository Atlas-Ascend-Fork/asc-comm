# RDV Dual-Control-Plane Batch Write Samples

This directory contains two functionally equivalent batch-write samples. Their data planes are identical (one
single-segment `WriteNbi` per WQE, weak ordering, only the last WQE of each channel per batch requests a CQE); they
differ only in the Host control-plane APIs used for channel setup:

- `rdv_hccl`: HCCL communicator path. Memory is registered with `HcclCommMemReg`, channels are described by
  `HcclChannelDesc`, and `MakeMultiChannelHandle(HcclComm, tag, ...)` creates the channels. Resources are owned by
  the communicator; the SQ depth is the library default.
- `rdv_hcomm`: HCOMM Endpoint path. `HcommEndpointCreate`/`HcommMemReg` create the endpoint and register memory,
  channels are described by `HcommChannelDesc` (`ubAttr.sqDepth=4096`), and `MakeMultiChannelHandle(EndpointHandle,
  ...)` creates the channels. Resources are released explicitly by the sample (`DestroyMultiChannelHandle`,
  `HcommMemUnreg`, `HcommEndpointDestroy`).

## Configuration

- Every WQE moves `DATA_SIZE` (1 MiB) in one segment; no multi-SGE.
- Each iteration submits 32 WQEs to every peer; `PERF_ITERATION_COUNT` (1) round runs, with no warmup.
- Multi-core: the build option `RDV_AIV_CORE_NUM` (1-56) gives each AIV core its own multi-channel handle (its own
  Jetty).
- All-to-all: every rank writes to all other ranks.

## Prerequisites

- Ascend 950PR or Ascend 950DT with at least two NPUs;
- an installed asc-comm development package containing the shared-Jetty control plane and the Batch APIs;
- a sourced CANN environment with `ASCEND_HOME_PATH` pointing to that installation.

## Build and Run

```bash
# HCCL path, one AIV core and eight ranks by default
bash examples/rdv/rdv_hccl/run.sh 1

# HCOMM Endpoint path with four AIV cores
bash examples/rdv/rdv_hcomm/run.sh 4
```

run.sh arguments: `<core_num> [rank_num] [ip:port] [first_rank] [local_ranks] [first_device]`, for example four
ranks:

```bash
bash examples/rdv/rdv_hccl/run.sh 1 4 127.0.0.1:8899 0 4 0
```

Manual build:

```bash
cmake -S examples/rdv/rdv_hccl -B build/rdv/rdv_hccl -DRDV_AIV_CORE_NUM=4
cmake --build build/rdv/rdv_hccl -j
./build/rdv/rdv_hccl/rdv_hccl 8
```

## Link layer selection

Both samples honour the `RDV_NET_LAYER` environment variable: unset scans the rank graph from layer 0 and uses the
first UB_CTP link of each peer; `1` starts from layer 1 (skipping layer 0). Every run prints the per-peer link
counts of every layer (`links=` total, `ubctp=` UB_CTP links):

```bash
RDV_NET_LAYER=1 bash examples/rdv/rdv_hccl/run.sh 1
```

The launcher forks one process per local rank and maps rank IDs to devices. Every rank writes to all peers; after
`Drain` and a global barrier the received data is verified byte by byte. Each rank prints one `rdv write verified` line plus the resolved SQ/CQ depths.
