# Hcomm Jetty WRITE Performance Sample

This sample demonstrates an URMA Jetty WRITE issued from an AIV kernel through the
`HcommPeer`, `HcommJetty::Write`, and `HcommJetty::Drain` interfaces in `hcomm_jetty.h`. Each rank
submits 1000 256-byte `Write` calls to every other rank, reports their average interface latency in device
cycles, and excludes the `Drain` wait after the submission loop. Remote data is placed in a slot
assigned to the source rank.

The source follows the single-file ASC organization used by `ain/basic_ring`:
`hcomm_jetty_write_perf.asc` contains host resource setup, kernel launch, and the device kernel. The
communication implementation remains based on `hcomm_jetty.h`; it is not replaced by the AIN API.

## Directory layout

```text
hcomm_jetty_write_perf
├── CMakeLists.txt
├── hcomm_jetty_write_perf.asc  // Host, kernel, and shared definitions
├── run.sh
├── README.md
└── README_en.md
```

## Flow

1. Rank 0 writes `HcclRootInfo` to a temporary file; other ranks poll the file before initializing the HCCL communicator.
2. The host registers the communication buffer and acquires a UBC_CTP channel through
   `COMM_ENGINE_AIV` for every non-local peer.
3. The host writes each channel handle into a device Jetty table and passes the local buffer
   address, externally supplied remote destination device address, and remote buffer index to the kernel.
4. The host launches one kernel. The kernel iterates over all peer contexts, constructs an
   `HcommPeer` and `HcommJetty` for each peer, records the cycles around the 1000-call 256-byte
   `Write` loop and divides by 1000, then calls `Drain` only after the timed loop.
5. Ranks synchronize with an HCCL barrier, then the host copies the buffer back and validates every
   peer pattern.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
cmake -S . -B build -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build -j
```

Replace `/usr/local/Ascend/cann` with the actual CANN installation path when needed.

## Run

```bash
./run.sh <nranks>
```

For two local devices:

```bash
./run.sh 2
```

Each rank prints the average latency of 1000 `Write` submissions per peer in device cycles. The
reported interval surrounds only the submission loop and excludes `Drain`.

To start one rank directly:

```text
./build/hcomm_jetty_write_perf <rank> <nranks> <root_info_file>
```

All ranks must print `Jetty WRITE validation passed` for a successful run.

## Supported scope

| Item | Description |
| --- | --- |
| Product | `Ascend 950PR / Ascend 950DT` |
| NPU architecture | `dav-3510` |
| Communication engine | `COMM_ENGINE_AIV` |
| Communication protocol | `COMM_PROTOCOL_UBC_CTP` |
| Deployment | Single-node multi-device |
