# HCCL One Multi Path Sample

## Overview

This sample demonstrates how to query a `COMM_PROTOCOL_UB_MEM` link through the HCCL Resource API and create, for each peer in the same communication domain,
two channels with `pathMode=1` and `pathMode=2`. Peers are processed serially; when processing a single peer, two streams
copy different data shards concurrently.

The sample runs one process per device. Each rank fills its local HCCL Buffer with its rank ID, then reads 4 KB from
other ranks and verifies the data. The executable directly launches all local rank processes; in a multi-machine deployment, each machine starts one process,
and the rank 0 process distributes `HcclRootInfo`.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
one_multi_path
├── CMakeLists.txt      // Build configuration file
├── main.cpp            // Host-side communication domain, channel, and verification flow
├── ubmem_data_copy.asc // Ascend C data copy kernel
├── README.md           // Chinese sample documentation
└── README_en.md        // English sample documentation
```

## Sample Description

### Functionality Description

Each rank first obtains its local HCCL Buffer and fills the first 4 KB with its rank ID. After the channels are created, each rank
reads the peer HCCL Buffer through the two channels for that peer, writes the results into the local verification buffer, and finally copies it to
the Host for byte-by-byte verification.

```text
Peer HCCL Buffer -> UB_MEM link -> Ascend C DataCopy -> local verification buffer -> host verification
```

The two channels for the same peer use the same communication endpoints and are distinguished by `ubMemAttr.pathMode`:

| Channel Order | Link Protocol | `pathMode` | Path Type | Data Range |
| --- | --- | --- | --- | --- |
| 1 | `COMM_PROTOCOL_UB_MEM` | `1` | one path | First 2 KB |
| 2 | `COMM_PROTOCOL_UB_MEM` | `2` | multi path | Second 2 KB |

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Two `COMM_PROTOCOL_UB_MEM` paths per peer (one path/multi path) reading concurrently |
| Invocation Method | Host-side Resource API for channel creation + Ascend C DataCopy Kernel |
| Data Scale | 4 KB read per peer (2 KB per path) |
| Deployment Mode | Single-node multi-card / multi-node multi-card |

### Implementation Flow

1. The executable directly launches local rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   ranks exchange `HcclRootInfo` through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`),
   and each rank initializes the same communication domain.
2. Each rank obtains its local HCCL Buffer and writes its rank ID, then registers the memory resources required by the channels through `HcclCommMemReg`.
3. Each rank scans the RankGraph layers and selects one `COMM_PROTOCOL_UB_MEM` link for each peer.
4. For each peer, two descriptors with `pathMode=1` and `pathMode=2` are built based on the same set of endpoints.
5. All peer channel descriptors are passed to `HcclChannelAcquire` at once to create the channels in a batch.
6. Peers are processed serially; the two channels of a single peer read the first and second 2 KB shards on two separate streams, and both kernels
   are submitted before the streams are synchronized, so concurrency is limited to the two paths of the same peer.
7. The local verification buffer is copied to the Host, where each byte is checked against the peer rank ID.
8. After all peer data is verified, a control-channel barrier synchronizes to ensure that all ranks finish remote reads before destroying communication resources.

## Compilation and Execution

Perform the following steps in the sample root directory to compile and run the sample. This sample supports NPU run mode only.

- Configure environment variables

  Configure the environment variables according to the CANN development toolkit [installation method](../../../../docs/quick_start_en.md).

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If not specified, the default installation directory is `/usr/local/Ascend`.

- Single-machine two-card run

  The executable internally forks all rank processes, with devices and ranks mapped one-to-one:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/one_multi_path tcp://127.0.0.1:29625 <nranks>
  ```

- Launch parameter description

  Single-machine mode:

  ```bash
  ./build/one_multi_path <ip:port> <nranks>
  ```

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Total number of ranks in the communication domain; must be an integer not less than 2 and not exceed the number of available NPUs on the host |

- Cross-machine run

  In a multi-machine deployment, each machine directly runs the executable, specifying the global rank and device per rank:

  ```bash
  ./build/one_multi_path <ip:port> <rank_size> <rank> <device>
  ```

  With two machines each having 4 cards (8 cards total), first run the first command on the rank 0 machine, then run on the other machine:

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

  `<ip:port>` points to the rank 0 machine on every host; `<rank_size>` is the total global rank count, `<rank>` is this process's global
  rank, and `<device>` is the local device ID.

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT products |

- Expected output

  Log order may vary due to multi-process concurrency. Each rank should complete data verification with other ranks, for example:

  ```text
  [rank 0] one_multi_path | channels_per_peer=2, channel_descs=2
  rank 0: peer=1, rank_graph_topo=5, protocol=UB_MEM, path_modes=1/2
  rank 0: dual-path data copy from remote rank 1 passed, bytes=4096
  [rank 0] one_multi_path | dual-path validation | PASS
  ```

  The sample succeeds when all ranks print `PASS` and the final output is `RESULT | Example=one_multi_path | Status=PASS`.

## Notes

- Running the sample requires at least 2 NPUs; a single-card environment supports compilation verification only.
- The default scenario requires Device 0 and Device 1 to be available, with a `COMM_PROTOCOL_UB_MEM` link between them. When
  `no UB_MEM link found` appears, it means no available `COMM_PROTOCOL_UB_MEM` link exists between the current rank and the target rank.
- The `rank_graph_topo` log only shows the RankGraph layer containing the selected UB_MEM link and does not participate in `pathMode` selection.
- In multi-machine runs, all machines must use the same `<rank_size>` and `<ip:port>` (pointing to rank 0), and the rank ranges must be
  complete and non-duplicated.
- The `<ip:port>` control channel is only used to distribute `HcclRootInfo` and Host-side synchronization, and does not participate in HCCL data link selection.
- When `HcclChannelAcquire` fails, locate the failure scope based on the preceding peer link-selection logs.