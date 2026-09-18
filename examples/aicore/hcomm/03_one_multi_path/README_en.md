# HCCL One Multi Path Sample

## Overview

This sample demonstrates how to query a `COMM_PROTOCOL_UB_MEM` link through HCCL Resource APIs and create two channels with `pathMode=1` and `pathMode=2` for every peer in one communication domain. Peers are processed serially; for each peer, separate data shards are copied concurrently on two streams.

The sample uses one process per device. Each rank fills its local HCCL Buffer with its rank ID, reads 4 KB from every peer, and verifies the data. The executable forks all local rank processes internally, and rank 0 distributes
`HcclRootInfo`.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
one_multi_path
├── CMakeLists.txt          // Build configuration file
├── main.cpp                // Host-side communication domain, channel, and verification flow
├── README.md               // Chinese documentation
├── README_en.md            // English documentation
└── ubmem_data_copy.asc     // Ascend C data copy kernel
```

## Sample Description

### Functionality

Each rank obtains its local HCCL Buffer and fills the first 4 KB with its rank ID. After the channels are created, each rank reads every peer HCCL Buffer through the two channels for that peer, writes the data to its local verification buffer, copies the buffer to the host, and verifies every byte.

```text
Peer HCCL Buffer -> UB_MEM link -> Ascend C DataCopy -> local verification buffer -> host verification
```

For the same peer, the two channels use the same communication endpoints and are distinguished by
`ubMemAttr.pathMode`:

| Channel Order | Link Protocol | `pathMode` | Path Type | Data Range |
| --- | --- | --- | --- | --- |
| 1 | `COMM_PROTOCOL_UB_MEM` | `1` | one path | First 2 KB |
| 2 | `COMM_PROTOCOL_UB_MEM` | `2` | multi path | Second 2 KB |

### Implementation Flow

1. The executable forks all rank processes (via `examples/aicore/utils/process_manager.h`); ranks exchange `HcclRootInfo` through a TCP control channel (`examples/aicore/utils/rank_sync.h`) and initialize the same communication domain.
2. Each rank obtains its local HCCL Buffer, fills it with the rank ID, and registers the memory resource required by the channels through `HcclCommMemReg`.
3. Each rank scans the RankGraph layers and selects one `COMM_PROTOCOL_UB_MEM` link for every peer.
4. For every peer, the sample builds two descriptors with the same endpoints and `pathMode=1` and `pathMode=2`.
5. All peer channel descriptors are passed to one `HcclChannelAcquire` call to create the channels in one batch.
6. Peers are processed serially. For each peer, the two channels read the first and second 2 KB shards on separate streams, and both kernels are submitted before either stream is synchronized. Concurrency is limited to the two paths of that peer.
7. The local verification buffer is copied to the host, where every byte is checked against the peer rank ID.
8. After all peer data is verified, a barrier (via `RankSyncContext`) ensures that every rank has completed its remote reads before communication resources are destroyed.

The `rank_graph_topo` log reports only the RankGraph layer containing the selected UB_MEM link. It does not
select `pathMode`.

## Build and Run

Perform the following steps in the sample root directory. This sample supports NPU run mode only.

- Set Environment Variables

  Configure the environment variables according to the CANN development toolkit
  [installation method](../../../../docs/quick_start_en.md).

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN installation directory. The default installation directory is
  > `/usr/local/Ascend`.

- Single-machine run

  The executable forks all rank processes internally; each rank is assigned to the corresponding NPU device:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/one_multi_path tcp://127.0.0.1:29625 <nranks>
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | Required | Control channel address that rank 0 listens on (`tcp://ip:port`); use different ports for parallel instances |
  | `<nranks>` | Required | Total number of ranks; must be >= 2 and not exceed available NPU count |

- Multi-machine run

  Each host runs the executable with per-rank arguments:

  ```bash
  ./build/one_multi_path <tcp://ip:port> <rank_size> <rank> <device>
  ```

  For eight devices across two hosts:

  ```bash
  # Host A: ranks 0-3, devices 0-3
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 0 0
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 1 1
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 2 2
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 3 3

  # Host B: ranks 4-7, devices 0-3
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 4 0
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 5 1
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 6 2
  ./build/one_multi_path tcp://<host_A_IP>:8899 8 7 3
  ```

  `<ip:port>` must point to the rank 0 host on every machine; `<rank_size>` is the global rank count; `<rank>` is this process's global rank ID; `<device>` is the local device ID.

- Build Options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture for Ascend 950PR/Ascend 950DT |

- Expected Output

  Log order can vary because the rank processes run concurrently. Every rank should validate every peer:

  ```text
  [rank 0] one_multi_path | channels_per_peer=2, channel_descs=2
  rank 0: peer=1, rank_graph_topo=5, protocol=UB_MEM, path_modes=1/2
  rank 0: dual-path data copy from remote rank 1 passed, bytes=4096
  [rank 0] one_multi_path | dual-path validation | PASS
  ```

  The sample succeeds when every rank obtains two different channel handles for every peer, verifies the data,
  and the final output is `RESULT | Example=one_multi_path | Status=PASS`.

## Notes

- The default run requires devices 0 and 1 and a `COMM_PROTOCOL_UB_MEM` link between them.
- For a multi-host run, every host must use the same `<rank_size>` and `<ip:port>` (pointing to rank 0), and the rank ranges must be complete and unique.
- `<ip:port>` is used only for distributing `HcclRootInfo` and host-side synchronization. It does not select the HCCL data link.
- If `HcclChannelAcquire` fails, use the preceding peer link-selection logs to narrow down the failing range.
- `no UB_MEM link found` means that no `COMM_PROTOCOL_UB_MEM` link is available between the local and target
  ranks.
