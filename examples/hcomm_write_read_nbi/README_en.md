# Hcomm WriteNbi/ReadNbi Point-to-Point Communication Sample (AIV Direct-Drive URMA)

## Overview

This sample demonstrates how to implement ultra-low latency point-to-point (P2P) communication between NPUs in an Ascend C Kernel using the **`WriteNbi`** and **`ReadNbi`** APIs of the `Hcomm` class, based on the **AIV Direct-Drive URMA** architecture. In this sample, two cards symmetrically execute `WriteNbi` + `ReadNbi`, differentiate data segments via address offsets, mutually write and read data, and finally validate the consistency of the results on the Host side.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
|---------|-----------------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
├── hcomm_write_read_nbi
│   ├── CMakeLists.txt              // CMake build file
│   ├── README.md                   // Sample documentation
│   └── hcomm_write_read_nbi.asc    // Ascend C sample implementation (Kernel + Host)
```

## Sample Description

### Sample Functionality
This sample focuses on demonstrating P2P communication interfaces under the **AIV Direct-Drive URMA** architecture. Unlike collective communication primitives (e.g., AlltoAll) that are initiated by the Host, **every data-plane communication in AIV Direct-Driven URMA no longer requires Host involvement**. They allow the NPU hardware to directly initiate URMA operations to the target NPU, bypassing traditional software protocol stacks and achieving direct Global Memory (GM) to GM access with ultra-low latency. This feature is highly suitable for latency-sensitive distributed training scenarios such as MoE (Dispatch/Combine) and Pipeline Parallelism.

| API | Data Flow | Semantic Description (AIV Direct-Drive URMA) |
|-----|-----------|---------------------------------------------|
| `WriteNbi` | Local GM → Remote GM | **AIV Direct-Drive URMA Write API**: Directly writes local Global Memory data to a specified address on the remote NPU, without requiring remote CPU intervention. |
| `ReadNbi` | Remote GM → Local GM | **AIV Direct-Drive URMA Read API**: Directly reads data from a specified address on the remote NPU into local Global Memory, without requiring remote CPU intervention. |

### Sample Implementation

#### 1. Host-Side Communication Domain Preparation
On the Ascend 950 series, the communication domain must be created in a multi-process manner (each process corresponds to one rank). The key steps are as follows, highlighting the AIV direct-drive configuration:

1. **Exchange RootInfo**: Rank 0 calls `HcclGetRootInfo` to obtain root information and sends it to Rank 1 via TCP.
2. **Create Communication Domain**: Each rank calls `HcclCommInitRootInfoConfig` to create the communication domain. **Note: In AIV direct-drive mode, there is no need to configure `hcclOpExpansionMode`.**
3. **Register Communication Memory**: Call `HcclCommMemReg` to register the local communication buffer with the communication domain. This memory information is automatically exchanged with the peer during channel creation.
4. **Obtain Link Endpoints**: Use `HcclRankGraphGetLayers` and `HcclRankGraphGetLinks` to obtain the physical link endpoint information from the local rank to the peer rank.
5. **Acquire P2P Channel (AIV Direct-Drive)**: Call `HcclChannelAcquire` to create the P2P channel to the peer. You must explicitly specify the engine as `COMM_ENGINE_AIV` and the protocol as `COMM_PROTOCOL_UBC_CTP` (i.e., the URMA protocol), along with the notification count and the memory handles to be exchanged.
6. **Obtain Remote Memory Address**: Call `HcclChannelGetRemoteMems` to retrieve the memory address registered by the peer, which serves as the remote target address for `WriteNbi`/`ReadNbi` in the Kernel.
7. **Download Context**: The Host pre-initializes seg0 (filling it with a pseudo-random pattern based on `rankId`), encapsulates the `ChannelHandle` and buffer addresses into `CommContext`, and downloads it to the GM of each card.

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
The communication process consists of three steps: `Init()` → `WriteNbi()`/`ReadNbi()` → `Drain()`. Both cards execute the same Kernel logic, differentiating the three data segments via address offsets:
- **seg0** `[0, DATA_SIZE)`: Local pattern (pre-initialized by the Host).
- **seg1** `[DATA_SIZE, 2*DATA_SIZE)`: Receives data written by the peer's `WriteNbi`.
- **seg2** `[2*DATA_SIZE, 3*DATA_SIZE)`: Receives data read back from the peer's seg0 by the local `ReadNbi`.

- **`Init`**: Allocates a Unified Buffer (UB) workspace (>= 512 bytes) to store internal Hcomm states such as WQE/CQE.
- **`WriteNbi` / `ReadNbi`**: Invokes the **AIV Direct-Drive URMA APIs** to enqueue communication tasks into the Send Queue (SQ). By default, `commit=true` triggers the doorbell immediately after enqueueing. For batch optimization, you can use `commit=false` to enqueue multiple tasks and call `Commit()` uniformly.
- **`Drain`**: Polls the Completion Queue (CQ) to wait for the communication tasks to finish, ensuring data visibility upon return.

```cpp
// Symmetric execution on both cards using AIV Direct-Drive URMA:
// 1. Local seg0 → Peer seg1 (WriteNbi)
hcomm_.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);

// 2. Peer seg0 → Local seg2 (ReadNbi)
hcomm_.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);

// 3. Wait for AIV hardware communication tasks to complete
hcomm_.Drain(channel);
```

#### 3. Invocation Implementation
Multi-process symmetric execution: Both cards launch the kernel simultaneously without requiring phased synchronization. After the Host pre-initializes seg0, it uses `TcpBarrier` to ensure both cards are ready before invoking the kernel using the `<<<>>>` kernel launch syntax.

### Validation Mechanism
- During Host pre-initialization of seg0, a pseudo-random pattern is generated using a Linear Congruential Generator (LCG, utilizing Knuth's multiplicative hash constant `0x9E3779B9U` and other parameters) to ensure the data source is distinguishable.
- After Kernel execution, the Host reads back `CommContext::testResult` via `aclrtMemcpy`.
- The Host further validates whether the data in seg1 (written by peer's `WriteNbi`) and seg2 (read by local `ReadNbi`) perfectly matches the peer's pattern. when the result code testResult == 0 (i.e. seg1/seg2 match the peer pattern), it prints test pass!.

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
This sample supports two execution methods:

**Method 1: Auto-Fork Multi-Process (Recommended)**
Execute directly. The main process will automatically fork two child processes (symmetrically executing WriteNbi + ReadNbi on two cards):
```bash
./demo
```

**Method 2: Manual Single-Process Execution with Arguments**
You can manually start Rank 0 and Rank 1 in two separate terminals:
```bash
# Terminal 1: Start rank 0 (bound to device 0)
./demo 0 2 tcp://127.0.0.1:29621

# Terminal 2: Start rank 1 (bound to device 1)
./demo 1 2 tcp://127.0.0.1:29621
```

### 4. Build Options Description
| Option | Allowed Values | Description |
|--------|----------------|-------------|
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU Architecture: `dav-3510` corresponds to Ascend 950PR / Ascend 950DT |

### 5. Expected Output
Upon successful execution, the terminal will output the following, indicating that the AIV Direct-Drive URMA communication was successful (symmetric write/read on both cards with consistent results):
```text
rank 0 test pass!
rank 1 test pass!
test pass!
```
> **Note:** A single-card environment only supports compilation verification. Running this sample requires at least 2 NPUs.