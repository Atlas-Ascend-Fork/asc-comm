# HCCL One Multi Path Sample

## Overview

This sample demonstrates how to query a `COMM_PROTOCOL_UB_MEM` link through HCCL Resource APIs and create two channels with `pathMode=1` and `pathMode=2` for every peer in one communication domain. Peers are processed serially; for each peer, separate data shards are copied concurrently on two streams.

The sample uses one process per device. Each rank fills its local HCCL Buffer with its rank ID, reads 4 KB from every peer, and verifies the data. `run.sh` starts the local rank processes, and rank 0 distributes
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
├── run.sh                  // Multi-rank launch script
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

1. Rank 0 creates and distributes one `HcclRootInfo`, and every rank initializes the same communication domain.
2. Each rank obtains its local HCCL Buffer, fills it with the rank ID, and registers the memory resource required by the channels through `HcclCommMemReg`.
3. Each rank scans the RankGraph layers and selects one `COMM_PROTOCOL_UB_MEM` link for every peer.
4. For every peer, the sample builds two descriptors with the same endpoints and `pathMode=1` and `pathMode=2`.
5. All peer channel descriptors are passed to one `HcclChannelAcquire` call to create the channels in one batch.
6. Peers are processed serially. For each peer, the two channels read the first and second 2 KB shards on separate streams, and both kernels are submitted before either stream is synchronized. Concurrency is limited to the two paths of that peer.
7. The local verification buffer is copied to the host, where every byte is checked against the peer rank ID.
8. After all peer data is verified, `HcclBarrier` ensures that every rank has completed its remote reads before communication resources are destroyed.

The `rank_graph_topo` log reports only the RankGraph layer containing the selected UB_MEM link. It does not
select `pathMode`.

## Build and Run

Perform the following steps in the sample root directory. This sample supports NPU run mode only.

- Set Environment Variables

  Configure the environment variables according to the CANN development toolkit
  [installation method](../../docs/quick_start_en.md).

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN installation directory. The default installation directory is
  > `/usr/local/Ascend`.

- Run on Two Devices

  By default, the script starts two processes on devices 0 and 1.

  ```bash
  cmake -S . -B build -DCMAKE_ASC_ARCHITECTURES=dav-3510
  cmake --build build -j
  bash run.sh
  ```

- Launch Options

  | Option | Default | Description |
  | --- | --- | --- |
  | `-pes` | `2` | Total number of ranks; it must be an integer greater than or equal to 2 |
  | `-ipport` | `tcp://127.0.0.1:8899` | Address used by rank 0 to distribute `HcclRootInfo`; use `tcp://[address]:port` for IPv6 |
  | `-gnpus` | `2` | Number of rank processes started on the current host |
  | `-fpe` | `0` | First rank ID on the current host |
  | `-fnpu` | `0` | First device ID on the current host |

- Run on Multiple Hosts

  For eight devices across two hosts, start the first command on the rank 0 host, followed by the second command on the other host:

  ```bash
  # Host A: ranks 0-3, devices 0-3
  bash run.sh -pes 8 -ipport tcp://<host_A_IP>:8899 -gnpus 4 -fpe 0 -fnpu 0

  # Host B: ranks 4-7, devices 0-3
  bash run.sh -pes 8 -ipport tcp://<host_A_IP>:8899 -gnpus 4 -fpe 4 -fnpu 0
  ```

  For 16 devices with eight devices per host, set `-pes` to `16`, `-gnpus` to `8`, and use `-fpe 0` and `-fpe 8` on the two hosts.

- Build Options

  | Option | Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture for Ascend 950PR/Ascend 950DT |

- Expected Output

  Log order can vary because the rank processes run concurrently. Every rank should validate every peer:

  ```text
  rank 0/2: device=0, channels_per_peer=2, channel_descs=2
  rank 0: peer=1, rank_graph_topo=5, protocol=UB_MEM, path_modes=1/2
  rank 0: dual-path data copy from remote rank 1 passed, bytes=4096
  rank 0: dual-path validation passed
  ```

  The sample succeeds when every rank obtains two different channel handles for every peer, verifies the data,
  and the script exits with 0.

## Notes

- The default run requires devices 0 and 1 and a `COMM_PROTOCOL_UB_MEM` link between them.
- For a multi-host run, every host must use the same `-pes`, and the rank ranges must be complete and unique.
- `-ipport` distributes `HcclRootInfo` only. It does not select the HCCL data link.
- If `HcclChannelAcquire` fails, use the preceding peer link-selection logs to narrow down the failing range.
- `no UB_MEM link found` means that no `COMM_PROTOCOL_UB_MEM` link is available between the local and target
  ranks.
