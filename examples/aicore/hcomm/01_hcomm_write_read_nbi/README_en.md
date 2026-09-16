# Hcomm AIV Direct-Drive URMA WriteNbi/ReadNbi Point-to-Point Communication Sample

## Overview

This sample demonstrates how to use the `WriteNbi` and `ReadNbi` APIs of the `Hcomm` class to implement low-latency point-to-point (P2P) communication between NPUs from an Ascend C AIV Kernel, based on the **AIV direct-drive URMA** architecture. The sample adopts a multi-rank ring topology: each rank establishes P2P channels with its neighboring ranks, separates data segments via address offsets, submits communication tasks directly from the Kernel side, and validates the communication results on the Host side.

The Host side is only responsible for creating the communication domain, registering communication memory, and acquiring channels; communication tasks are submitted directly by the AIV Kernel, and the Host does not need to participate in each data-plane transfer. This mode is suitable for latency-sensitive scenarios such as MoE Dispatch/Combine and pipeline parallelism.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
|---------|-----------------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
hcomm_write_read_nbi
├── CMakeLists.txt              // CMake build file
├── hcomm_write_read_nbi.asc    // Host resource setup and Kernel invocation
├── hcomm_write_read_nbi_kernel.cpp // Hcomm calls from the AIV Kernel
├── hcomm_rw_def.h              // Definitions shared by Host and Kernel
├── README.md                   // Chinese sample documentation
└── README_en.md                // English sample documentation
```

## Sample Description

### Functionality Description

| API | Data Flow | Semantic Description (AIV Direct-Drive URMA) |
|-----|-----------|----------------------------------------------|
| `WriteNbi` | Local GM → Remote GM | AIV direct-drive URMA write API: directly writes local Global Memory data to a specified address on the remote NPU, without requiring remote CPU intervention. |
| `ReadNbi` | Remote GM → Local GM | AIV direct-drive URMA read API: directly reads data from a specified address on the remote NPU into local Global Memory, without requiring remote CPU intervention. |

Each rank's communication buffer is divided into three segments, distinguished by address offsets:

- **seg0** `[0, DATA_SIZE)`: Local pattern (pre-initialized by the Host using a `rank + index` pattern, with the first byte set to rankId, ensuring that data sources from different ranks are distinguishable)
- **seg1** `[DATA_SIZE, 2*DATA_SIZE)`: Receives data written by `prev` through `WriteNbi`
- **seg2** `[2*DATA_SIZE, 3*DATA_SIZE)`: Receives data read back from `next` by the local `ReadNbi`

After Kernel execution, the Host reads back the results and validates whether seg1 and seg2 match the peer patterns exactly.

### Sample Specifications

| Item | Description |
|------|-------------|
| Communication Mode | Ring topology AIV direct-drive URMA point-to-point communication |
| Invocation Method | `Hcomm::WriteNbi`/`ReadNbi` within AIV Kernel |
| Process Launch | Executable directly launches, internally forks multiple ranks |
| Supported Rank Count | >= 2, not exceeding the number of available NPUs |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`); ranks exchange root info and create the communication domain through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`). **Note: In AIV direct-drive mode, there is no need to configure `hcclOpExpansionMode`.**
2. Call `HcclCommMemReg` to register the local communication buffer with the communication domain; memory information is automatically exchanged with the peer during channel creation.
3. Use `HcclRankGraphGetLayers` and `HcclRankGraphGetLinks` to obtain link endpoints for `prev` and `next`.
4. Call `HcclChannelAcquire` to create P2P channels, specifying `COMM_ENGINE_AIV` as the engine and `COMM_PROTOCOL_UB_CTP` as the protocol (URMA protocol).
5. Call `HcclChannelGetRemoteMems` to retrieve the memory addresses registered by peer ranks, which serve as remote target addresses for `WriteNbi`/`ReadNbi` in the Kernel.
6. The Host pre-initializes seg0, encapsulates the `ChannelHandle` and buffer addresses into `CommContext`, and downloads it to the GM of each card.
7. The Kernel side executes `Init()` to allocate UB workspace, then submits communication tasks: writes local seg0 to `next`'s seg1 through `WriteNbi`, and reads `next`'s seg0 into local seg2 through `ReadNbi`, finally calling `Drain()` to wait for communication tasks to complete.
8. The Host synchronizes via control channel barrier, then reads back results and validates seg1 and seg2 patterns.

```cpp
// Example AIV direct-drive URMA operations on one rank:
// 1. Local seg0 → Peer seg1 (WriteNbi)
hcomm_.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);

// 2. Peer seg0 → Local seg2 (ReadNbi)
hcomm_.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);

// 3. Wait for AIV hardware communication tasks to complete
hcomm_.Drain(channel);
```

## Compilation and Execution

Follow the steps below in the root directory of this sample to compile and run it. This sample only supports NPU execution mode.

- Configure environment variables

  Configure the environment variables according to the installation method of the CANN development kit on your current environment.

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If not specified, it defaults to `/usr/local/Ascend`.

- Run the sample

  Execute the following commands in the sample directory. The executable internally forks all rank processes, so it can be run directly:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/hcomm_write_read_nbi tcp://127.0.0.1:29623 <nranks>
  ```

- Launch parameter description

  | Parameter | Default | Description |
  |-----------|---------|-------------|
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of ranks to launch; must be >= 2 and not exceed the number of available NPUs |

- Build options description

  | Option | Allowed Values | Description |
  |--------|----------------|-------------|
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU architecture: `dav-3510` corresponds to Ascend 950PR / Ascend 950DT |

- Expected output

  After a successful multi-card run, the terminal will output the following, indicating that AIV direct-drive URMA communication completed successfully (write/read results are consistent):

  ```text
  [rank 0] hcomm_write_read_nbi | sent to rank 1, received from rank 1 | PASS
  [rank 1] hcomm_write_read_nbi | sent to rank 0, received from rank 0 | PASS
  RESULT | Example=hcomm_write_read_nbi | Status=PASS
  ```

## Notes

- Running the sample requires at least 2 NPUs; a single-card environment only supports compilation verification.
- The UB workspace allocated by `Init()` on the Kernel side must be >= 512 bytes, used to store internal Hcomm states such as WQE/CQE.
- `WriteNbi`/`ReadNbi` defaults to `commit=true`, triggering the doorbell immediately after enqueueing; for batch optimization, use `commit=false` to enqueue multiple tasks and call `Commit()` uniformly. Data visibility is only guaranteed after `Drain()` returns.
- When `nranks == 2`, `prev` and `next` refer to the same peer rank, so seg1 and seg2 are validated against the same peer pattern.
- The current sample has established P2P channels in both the `prev` and `next` directions, but only executes data-plane operations in the `next` direction by default. To extend to full all-to-neighbor communication, reuse the established `prev` channel: launch the Kernel twice, first using the `prev` channel to execute `ReadNbi` reading `prev`'s seg0 into local seg2, then using the `next` channel to execute `WriteNbi` writing local seg0 into `next`'s seg1, so that each rank's seg1 and seg2 both match `prev`'s pattern. When extending, keep the Host-side barriers before and after Kernel execution; if data segment offsets are adjusted, update the expected rank and offset used by Host-side pattern validation accordingly.
