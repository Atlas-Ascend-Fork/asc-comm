# DeepEP Handle WRITE Performance Sample

This sample issues an URMA Jetty WRITE directly through a complete local copy of DeepEP
`handle.hpp`, using `deep_ep::handle::HcommPeer`, `HcommJetty::put`, and `HcommJetty::drain`. Each rank
submits 1000 256-byte `put` calls to every other rank, reports their average interface latency in device
cycles, and excludes the `drain` wait after the submission loop. Remote data is placed in a slot
assigned to the source rank.

The source follows the single-file ASC organization used by `ain/basic_ring`:
`hcomm_handle_write_perf.asc` contains host resource setup, kernel launch, and the device kernel.
`handle.hpp` and its DeepEP dependencies are copied into this sample, so the build does not depend on
external DeepEP source files.

## Directory layout

```text
hcomm_handle_write_perf
├── CMakeLists.txt
├── hcomm_handle_write_perf.asc  // Host, kernel, and shared definitions
├── handle.hpp                   // Complete local copy of DeepEP handle.hpp
├── deep_ep/                     // Direct dependencies of handle.hpp
├── run.sh
├── README.md
└── README_en.md
```

## Flow

1. Rank 0 writes `HcclRootInfo` to a temporary file; other ranks poll the file before initializing the HCCL communicator.
2. The host registers the communication buffer and acquires a UBC_CTP channel through
   `COMM_ENGINE_AIV` for every non-local peer.
3. The host writes each channel handle into a device Jetty table and passes the local buffer
   address and destination slot offset to the kernel. The sample verifies that the user buffer is last
   in the remote registration list.
4. The host launches one kernel. The kernel iterates over all peer contexts, constructs an
   `deep_ep::handle::HcommPeer` and `HcommJetty` for each peer, records the cycles around the 1000-call 256-byte
   `put` loop and divides by 1000, then calls `drain` only after the timed loop.
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

Each rank prints the average latency of 1000 `put` submissions per peer in device cycles. The
reported interval surrounds only the submission loop and excludes `drain`.

To start one rank directly:

```text
./build/hcomm_handle_write_perf <rank> <nranks> <root_info_file>
```

All ranks must print `Handle WRITE validation passed` for a successful run.

## Notes

`handle.hpp` always selects the last registered buffer for each remote Jetty. The sample checks that
the user buffer tagged `shareBuf` is last in the remote registration list and exits if that assumption is violated.

## Supported scope

| Item | Description |
| --- | --- |
| Product | `Ascend 950PR / Ascend 950DT` |
| NPU architecture | `dav-3510` |
| Communication engine | `COMM_ENGINE_AIV` |
| Communication protocol | `COMM_PROTOCOL_UBC_CTP` |
| Deployment | Single-node multi-device |
