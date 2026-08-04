# Hcomm AIV Direct-Drive URMA WriteNbi/ReadNbi Point-to-Point Communication Sample

## Overview

This sample demonstrates low-latency point-to-point (P2P) communication between NPUs from an Ascend C AIV Kernel. It uses the `WriteNbi` and `ReadNbi` APIs of `Hcomm` over the AIV direct-drive URMA path. The number of ranks is specified by `./demo [nranks]`; when no argument is provided, the sample starts two ranks by default. Each rank builds P2P channels to its neighboring ranks in a ring topology, uses address offsets to separate data segments, and validates the communication results on the Host.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
|---------|-----------------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
├── hcomm_write_read_nbi
│   ├── CMakeLists.txt              // CMake build file
│   ├── README.md                   // Sample documentation
│   ├── README_en.md                // English sample documentation
│   ├── hcomm_write_read_nbi.asc         // Host resource setup and Kernel invocation
│   ├── hcomm_write_read_nbi_kernel.cpp  // Hcomm calls from the AIV Kernel
│   ├── hcomm_rw_def.h              // Definitions shared by Host and Kernel
│   ├── utils.cpp                   // TCP helper implementation
│   └── utils.h                     // TCP helper declarations
```

## Sample Description

### Sample Functionality
This sample focuses on P2P communication over the **AIV direct-drive URMA** path. The Host creates the communication domain, registers communication memory, and acquires the channel. The AIV Kernel then submits the communication operations directly, without requiring the Host to participate in each data-plane transfer. This mode is suitable for latency-sensitive workloads such as MoE Dispatch/Combine and pipeline parallelism.

| API | Data Flow | Semantic Description (AIV Direct-Drive URMA) |
|-----|-----------|---------------------------------------------|
| `WriteNbi` | Local GM → Remote GM | **AIV Direct-Drive URMA Write API**: Directly writes local Global Memory data to a specified address on the remote NPU, without requiring remote CPU intervention. |
| `ReadNbi` | Remote GM → Local GM | **AIV Direct-Drive URMA Read API**: Directly reads data from a specified address on the remote NPU into local Global Memory, without requiring remote CPU intervention. |

### Sample Implementation

#### 1. Host-Side Communication Domain Preparation
On the Ascend 950 series, the communication domain must be created in a multi-process manner (each process corresponds to one rank). The key steps are as follows, highlighting the AIV direct-drive configuration:

1. **Exchange RootInfo**: Rank 0 calls `HcclGetRootInfo` to obtain root information and sends it to the other ranks via TCP.
2. **Create Communication Domain**: Each rank calls `HcclCommInitRootInfoConfig` to create the communication domain. **Note: In AIV direct-drive mode, there is no need to configure `hcclOpExpansionMode`.**
3. **Register Communication Memory**: Call `HcclCommMemReg` to register the local communication buffer with the communication domain. This memory information is automatically exchanged with the peer during channel creation.
4. **Build the TCP Ring Topology**: Each rank listens on `BASE_PORT + rank`, actively connects to `next = (rank + 1) % nranks`, and accepts the connection from `prev = (rank - 1 + nranks) % nranks`. This ring is used for Host-side barriers after RootInfo exchange, ensuring that all ranks advance through key phases together.
5. **Obtain Link Endpoints**: Use `HcclRankGraphGetLayers` and `HcclRankGraphGetLinks` to obtain physical link endpoint information from the local rank to both `prev` and `next`.
6. **Acquire P2P Channels (AIV Direct-Drive)**: Call `HcclChannelAcquire` to create P2P channels to neighboring ranks. Specify `COMM_ENGINE_AIV` as the engine and `COMM_PROTOCOL_UBC_CTP` as the URMA protocol, and pass the memory handles to be exchanged.
7. **Obtain Remote Memory Address**: Call `HcclChannelGetRemoteMems` to retrieve the memory addresses registered by neighboring ranks, which serve as remote target addresses for `WriteNbi`/`ReadNbi` in the Kernel.
8. **Download Context**: The Host pre-initializes seg0 (filling it with a pseudo-random pattern based on `rankId`), encapsulates the `ChannelHandle` and buffer addresses into `CommContext`, and downloads it to the GM of each card.

```cpp
// 1. Each rank creates the communication domain (AIV direct-drive mode does NOT require hcclOpExpansionMode)
HcclCommConfig config;
HcclCommConfigInit(&config);
config.hcclWorldRankID = rank;
HcclCommInitRootInfoConfig(nranks, &rootInfo, rank, &config, &comm);

// 2. Register communication memory
HcclCommMemReg(comm, "shareBuf", &regMem, &memHandle);

// 3. Obtain link endpoints
HcclRankGraphGetLinks(comm, layerId, rank, peerRank, &links, &linkNum);

// 4. Acquire AIV Direct-Drive URMA P2P Channel
channelDesc.remoteRank = peerRank;
channelDesc.channelProtocol = COMM_PROTOCOL_UBC_CTP; // URMA protocol
channelDesc.memHandles = &memHandle;
channelDesc.memHandleNum = 1;
HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &channel); // Specify COMM_ENGINE_AIV

// 5. Obtain remote memory address
HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags);
```

#### 2. Kernel-Side Execution
The communication process consists of three steps: `Init()` → `WriteNbi()`/`ReadNbi()` → `Drain()`. Each rank executes the same Kernel logic, differentiating the three data segments via address offsets:
- **seg0** `[0, DATA_SIZE)`: Local pattern (pre-initialized by the Host).
- **seg1** `[DATA_SIZE, 2*DATA_SIZE)`: Receives data written by the peer's `WriteNbi`.
- **seg2** `[2*DATA_SIZE, 3*DATA_SIZE)`: Receives data read back from the peer's seg0 by the local `ReadNbi`.

- **`Init`**: Allocates a Unified Buffer (UB) workspace (>= 512 bytes) to store internal Hcomm states such as WQE/CQE.
- **`WriteNbi` / `ReadNbi`**: Invokes the **AIV Direct-Drive URMA APIs** to enqueue communication tasks into the Send Queue (SQ). By default, `commit=true` triggers the doorbell immediately after enqueueing. For batch optimization, you can use `commit=false` to enqueue multiple tasks and call `Commit()` uniformly.
- **`Drain`**: Polls the Completion Queue (CQ) to wait for the communication tasks to finish, ensuring data visibility upon return.

```cpp
// Example AIV Direct-Drive URMA operations on one rank:
// 1. Local seg0 → Peer seg1 (WriteNbi)
hcomm_.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);

// 2. Peer seg0 → Local seg2 (ReadNbi)
hcomm_.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);

// 3. Wait for AIV hardware communication tasks to complete
hcomm_.Drain(channel);
```

#### 3. Ring Topology Communication Flow
The runtime rank count is `nranks`. Each rank computes its neighbors as follows:
- `prev = (rank - 1 + nranks) % nranks`
- `next = (rank + 1) % nranks`

The Host prepares communication contexts for both `prev` and `next`. The current sample uses only the `next` direction in the data plane: during Kernel execution, each rank writes its local seg0 to `next`'s seg1 through `WriteNbi`, and reads `next`'s seg0 into its local seg2 through `ReadNbi`. Therefore, seg1 is written by `prev`, while seg2 is read from `next`. When `nranks == 2`, `prev` and `next` refer to the same peer rank, so seg1 and seg2 are validated against the same peer pattern.

The Host runs TCP ring barriers before and after Kernel execution so that no rank validates data before all ranks have reached the same communication phase.

#### 4. How to Extend to All-to-Neighbor
The sample already establishes P2P channels in both the `prev` and `next` directions, but its default data plane only uses the `next` direction. To extend it into full all-to-neighbor communication, reuse the established `prev` channel so that each rank covers both "read from predecessor" and "write to successor" directions.

One direct implementation is to launch the Kernel twice: first use the `prev` channel to execute `ReadNbi`, reading `prev`'s seg0 into the local seg2; then use the `next` channel to execute `WriteNbi`, writing the local seg0 into `next`'s seg1. With this flow, each rank's seg1 is written by `prev` through `WriteNbi`, and seg2 is read from `prev` by the local `ReadNbi`; both segments should match `prev`'s pattern. The Host should read back the `testResult` from the communication context used by each Kernel launch and continue validating the seg1/seg2 patterns.

Keep the Host-side barriers before and after Kernel execution when extending the sample. The pre-Kernel barrier ensures all ranks have completed memory registration, channel acquisition, and context initialization. The post-Kernel barrier ensures all ranks have completed `Drain` for the corresponding direction before the Host reads back `testResult` and validates data. If new data segment offsets are added or existing offsets are changed, update the Host-side expected rank and offset used by pattern validation accordingly.

### Validation Mechanism
- During Host pre-initialization of seg0, a pseudo-random pattern is generated using a Linear Congruential Generator (LCG, utilizing Knuth's multiplicative hash constant `0x9E3779B9U` and other parameters) to ensure the data source is distinguishable.
- After Kernel execution, the Host reads back `CommContext::testResult` via `aclrtMemcpy`.
- The Host then checks that seg1 (written by `prev` through `WriteNbi`) and seg2 (read from `next` by the local `ReadNbi`) match the corresponding peer patterns. When `testResult` is 0, it prints `test pass!`.

## Compilation and Execution

Follow the steps below in the root directory of this sample to compile and run it.

### 1. Configure Environment Variables
Configure the environment variables according to the installation method of the CANN development kit on your current environment:
```bash
source ${install_path}/cann/set_env.sh
```
> **Note:** `${install_path}` is the CANN package installation directory. If not specified, it defaults to `/usr/local/Ascend`.

### 2. Build the Project

Execute the following commands in the sample directory:
```bash
mkdir -p build && cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

### 3. Execute the Sample
This sample starts ranks through automatic multi-process fork. The command format is:

```bash
./demo [nranks]
```

`nranks` is the number of ranks to start. It must be greater than or equal to 2 and should not exceed the number of available NPUs. If omitted, the sample starts two ranks by default:

```bash
# Default two-rank run
./demo

# Explicit two-rank run
./demo 2

# Four-rank ring topology
./demo 4
```

### 4. Build Options Description
| Option | Allowed Values | Description |
|--------|----------------|-------------|
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU Architecture: `dav-3510` corresponds to Ascend 950PR / Ascend 950DT |

### 5. Expected Output
For a successful multiple-rank run, the terminal will output the following, indicating that AIV Direct-Drive URMA communication completed successfully and the write/read results are consistent:
```text
rank 0 test pass!
rank 1 test pass!
test pass!
```
> **Note:** A single-card environment only supports compilation verification. Running this sample requires at least 2 NPUs.
