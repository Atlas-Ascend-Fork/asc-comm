# Hcomm Multi-Channel Shared-Queue Sample

## Overview

This sample demonstrates how to create a multi-rank communication domain from `HcclRootInfo` and use
`HcclChannelAcquireWithConfig` to let multiple logical Channels under the same local Endpoint share communication
queues. The Host registers communication memory, queries RankGraph, groups and acquires Channels. The AIV Kernel then
uses these Channels to write data to different peers, and the Host verifies the results.

Rank 0 creates `HcclRootInfo` and distributes it to the other ranks over TCP. Each rank initializes its communication
domain with the same `HcclRootInfo`.

## Supported Products and Versions

| Product | CANN Software Version |
|---------|-----------------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
├── hcomm_shared_queue_multi_channel
│   ├── CMakeLists.txt                          // CMake build file
│   ├── README.md                               // Chinese documentation
│   ├── README_en.md                            // English documentation
│   ├── hcomm_shared_queue_multi_channel.asc    // AIV Kernel and launch wrapper
│   ├── hcomm_shared_queue_multi_channel.cpp    // Host control plane and validation
│   └── run.sh                                  // Multi-process build and run script
```

## Sample Description

### Resource Model

```text
Local rank
  Endpoint group 0
    Channel(peer A) --+
    Channel(peer B) --+-- shared queue group 0

  Endpoint group 1
    Channel(peer C) ----- queue group 1
```

The control plane selects the nearest `COMM_PROTOCOL_UBC_CTP` link for every peer from RankGraph and groups links by
their local Endpoint. Endpoint identity is determined by `protocol`, `commAddr.type`, and `commAddr.eid`.

- All `HcclChannelDesc` objects in one group have the same `localEndpoint`. They are acquired in one
  `HcclChannelAcquireWithConfig` call and share communication queues.
- Each local Endpoint group uses a separate `HcclChannelAcquireWithConfig` call.

The essential shared-queue configuration is:

```cpp
HcclChannelConfig config = nullptr;
HcclChannelConfigCreate(&config);
HcclChannelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1);
HcclChannelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, sharedTag);
HcclChannelAcquireWithConfig(
    comm, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
HcclChannelConfigDestroy(config);
```

### Execution Flow

```text
Rank 0 calls HcclGetRootInfo
  -> Distribute the same HcclRootInfo over TCP
  -> Each rank calls HcclCommInitRootInfo
  -> Register the receive buffer and notify with HcclCommMemReg
  -> Query RankGraph and select one UBC_CTP link for every peer
  -> Group Channel descriptors by localEndpoint
  -> Acquire logical Channels sharing communication queues for every group
  -> Use the Channels to all peers in sequence for two cycles
  -> Wait for the remote notify and validate received data in every round
```

The Kernel uses `COMM_PROTOCOL_UBC_CTP`, while the control plane uses `COMM_ENGINE_AIV`. It calls `Lock` to acquire
the shared queue lock and load the queue indices, then calls `Unlock` to store the indices and release the lock after
`WriteWithNotifyNbi<false>`, `Commit`, and `Drain`. The Kernel reports its result to the Host, and the remote Host polls
the notify in registered memory and validates the received data.

The sample calls `HcclBarrier` before and after every transfer so that all ranks clear their receive buffers and
notifications before the next round.

### Multi-Channel Validation

The sample creates a Channel from the local rank to every peer and runs two all-peer transfer cycles. In every cycle,
rank `i` writes to `(i + offset) % rankSize` and receives from `(i - offset + rankSize) % rankSize`. When shared queues
are present, multiple Channels in the same group are used alternately, exercising logical Channel switching over the
shared communication queues.

## Build and Run

### 1. Configure the Environment

```bash
source ${install_path}/cann/set_env.sh
```

`${install_path}` is the CANN installation directory. The default installation location is usually
`/usr/local/Ascend`.

### 2. Run on Multiple Devices in One Host

Run the following commands in the sample directory:

```bash
cd examples/hcomm_shared_queue_multi_channel
bash run.sh
```

`run.sh` accepts the following arguments:

| Argument | Description |
|----------|-------------|
| `$1` | Global rank count. The default is `2`. |
| `$2` | Bytes transferred in each operation. The default is `4096`. |
| `$3` | TCP address used by rank 0 to distribute RootInfo. The default is `tcp://127.0.0.1:8899`. |
| `$4` | First global rank started on this host. The default is `0`. |
| `$5` | Number of ranks started on this host. The default is `$1`. |

For example, run on four devices:

```bash
bash run.sh 4 4096
```

On one host, global ranks map to devices with the same index. You can also build and start each rank manually:

```bash
cmake -S . -B build -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build -j

./build/demo 2 0 0 4096 &
./build/demo 2 1 1 4096 &
wait
```

The `demo` arguments are `<rank_size> <rank> <device> <bytes> [ipport]`.

### 3. Run on Multiple Hosts

Use the same `rank_size`, transfer size, RootInfo address, and program version on all hosts. The last two `run.sh`
arguments specify the global rank range assigned to the current host. Local device numbering starts from `0`.

For example, when two hosts run two ranks each and rank 0 is on `192.168.1.10`:

```bash
# Host A: global ranks 0-1
bash run.sh 4 4096 tcp://192.168.1.10:8899 0 2

# Host B: global ranks 2-3
bash run.sh 4 4096 tcp://192.168.1.10:8899 2 2
```

Start the host containing rank 0 first. Other ranks retry their connection to rank 0 for a limited period.

### 4. Expected Output

After every rank completes two data validation cycles, each rank prints one result line:

```text
rank 0 pass, exercised 1 peer channels for 2 cycles, bytes 4096.
rank 1 pass, exercised 1 peer channels for 2 cycles, bytes 4096.
ranks 0-1 run success.
```

## Runtime Constraints

- `rank_size` must be at least 2.
- Channels under the same local Endpoint share communication queues. RankGraph links are grouped by local Endpoint; Channels in the same group share communication queues.
- For multi-host execution, global rank ranges must not overlap and must completely cover `0..rank_size-1`.
- All ranks must use the same RootInfo, rank count, and program version.
- A usable CANN, HCCL, and NPU environment is required at runtime.
