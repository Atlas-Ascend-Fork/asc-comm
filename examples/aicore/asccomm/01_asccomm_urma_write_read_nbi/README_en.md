# CommRW AIV Direct-Drive URMA write_nbi/read_nbi Point-to-Point Communication Sample

## Overview

This sample demonstrates how to use the `write_nbi` and `read_nbi` APIs of the `asc::asccomm` class to implement low-latency point-to-point (P2P) communication between NPUs from an Ascend C AIV Kernel, based on the **AIV direct-drive URMA** architecture. The sample adopts a multi-rank ring topology: each rank establishes
P2P channels with its neighboring ranks, separates data segments via address offsets, submits communication tasks directly from the Kernel side, and
validates the communication results on the Host side.

The Host side is only responsible for creating the communication domain, registering communication memory, and acquiring channels; communication tasks are submitted directly by the AIV Kernel, and the Host does not need to participate in each data-plane transfer.
This mode is suitable for latency-sensitive scenarios such as MoE Dispatch/Combine and pipeline parallelism.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
01_asccomm_urma_write_read_nbi
├── CMakeLists.txt              // CMake build file
├── write_read_nbi.asc    // Host resource setup and Kernel invocation
├── write_read_nbi_kernel.cpp // CommRW calls from the AIV Kernel
├── asccomm_rw_def.h              // Definitions shared by Host and Kernel
├── README.md                   // Chinese sample documentation
└── README_en.md                // English sample documentation
```

## Sample Description

### Functionality Description

| API | Data Flow | Semantic Description (AIV Direct-Drive URMA) |
| --- | --- | --- |
| `write_nbi` | Local GM → Remote GM | AIV direct-drive URMA write API: directly writes local Global Memory data to a specified address on the remote NPU, without requiring remote CPU intervention. |
| `read_nbi` | Remote GM → Local GM | AIV direct-drive URMA read API: directly reads data from a specified address on the remote NPU into local Global Memory, without requiring remote CPU intervention. |

Each rank's communication buffer is divided into three segments, distinguished by address offsets:

- **seg0** `[0, DATA_SIZE)`: Local pattern (pre-initialized by the Host using a `rank + index` pattern, with the first byte set to the rank number, ensuring that data sources from different ranks are distinguishable)
- **seg1** `[DATA_SIZE, 2*DATA_SIZE)`: Receives data written by `prev` through `write_nbi`
- **seg2** `[2*DATA_SIZE, 3*DATA_SIZE)`: Receives data read back from `next` by the local `read_nbi`

After Kernel execution, the Host reads back the results and validates whether seg1 and seg2 match the peer patterns exactly.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Ring topology AIV direct-drive URMA point-to-point communication |
| Invocation Method | `asccomm::write_nbi`/`read_nbi` within AIV Kernel |
| Process Launch | Executable directly launches, internally forks multiple ranks |
| Supported Rank Count | >= 2, not exceeding the number of available NPUs |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   ranks exchange root info through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`) and
   create the communication domain. **Note: In AIV direct-drive mode, there is no need to configure `hcclOpExpansionMode`.**
2. Call `HcclCommMemReg` to register the local communication buffer with the communication domain; memory information is automatically exchanged with the peer during channel creation.
3. Use `HcclRankGraphGetLayers` and `HcclRankGraphGetLinks` to obtain link endpoints for `prev` and `next`.
4. Call `HcclChannelAcquire` to create P2P channels, specifying `COMM_ENGINE_AIV` as the engine and `COMM_PROTOCOL_UB_CTP` as the protocol (URMA protocol).
5. Call `HcclChannelGetRemoteMems` to retrieve the memory addresses registered by peer ranks, which serve as remote target addresses for `write_nbi`/`read_nbi` in the Kernel.
6. The Host pre-initializes seg0, encapsulates the `ChannelHandle` and buffer addresses into `comm_context`, and downloads it to the GM of each card.
7. The Kernel side executes `init()` to allocate UB workspace, then submits communication tasks: writes local seg0 through `write_nbi` to `next`'s
   seg1, and reads `next`'s seg0 into local seg2 through `read_nbi`, finally calling `drain()` to wait for communication tasks to complete.
8. The Host synchronizes via control channel barrier, then reads back results and validates seg1 and seg2 patterns.

```cpp
// Example AIV direct-drive URMA operations on one rank:
// 1. Local seg0 → Peer seg1 (write_nbi)
asccomm.write_nbi(channel, remote_buf + DATA_SIZE, local_buf, DATA_SIZE);

// 2. Peer seg0 → Local seg2 (read_nbi)
asccomm.read_nbi(channel, local_buf + 2 * DATA_SIZE, remote_buf, DATA_SIZE);

// 3. Wait for AIV hardware communication tasks to complete
asccomm.drain(channel);
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
  ./write_read_nbi tcp://127.0.0.1:29623 <nranks>
  ```

- Launch parameter description

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of ranks to launch; must be >= 2 and not exceed the number of available NPUs |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU architecture: `dav-3510` corresponds to Ascend 950PR&950DT products |

- Expected output

  After a successful multi-card run, the terminal will output the following, indicating that AIV direct-drive URMA communication completed successfully (write/read results are consistent):

  ```text
  [rank 0] write_read_nbi | sent to rank 1, received from rank 1 | PASS
  [rank 1] write_read_nbi | sent to rank 0, received from rank 0 | PASS
  RESULT | Example=write_read_nbi | Status=PASS
  ```

## Troubleshooting Common Runtime Failures

When a run fails, check the error message printed to the terminal (including the `ret=N` error code and the `hint` text) and locate the issue using the table below:

| Error Message | Possible Cause | Action |
| --- | --- | --- |
| `invalid control endpoint "..."` | Incorrect endpoint format | Use the `tcp://ip:port` or `tcp://[ip]:port` format |
| `listen <ip>:<port> failed: Address already in use` | The port is occupied by another process | Use `ss -ltnp` or `lsof -i:<port>` to find the process occupying the port, or use another port |
| `connect <ip>:<port> failed after N attempts` | rank 0 has not started, or the port is blocked by a firewall | Ensure the rank 0 process has started; check that the firewall/security group allows the port |
| `hccl call failed, ret=N` | HCCL runtime error (e.g., peer not ready, network unreachable) | Ensure all rank processes are healthy; locate the error code using the `hint` text and plog device logs |
| `init failed` reported by the kernel | Insufficient UB workspace (>= 512 bytes required) | Increase `ASCCOMM_WORKSPACE_SIZE` (see `asccomm_rw_def.h`) |
| `channels not ready after N retries` | Channel establishment timed out | Ensure the peer rank processes are alive and the network is reachable; check HCCL/HCOMM device logs |

> **Note:** The exact semantics of the `ret=N` error codes are subject to the HCCL/ACL error code documentation of the corresponding CANN version and are not expanded one by one in this document.

## Notes

- Running the sample requires at least 2 NPUs; a single-card environment only supports compilation verification.
- The UB workspace allocated by `init()` on the Kernel side must be >= 512 bytes, used to store internal asccomm states such as WQE/CQE.
- `write_nbi`/`read_nbi` defaults to `commit=true`, triggering the doorbell immediately after enqueueing; for batch optimization, use
  `commit=false` to enqueue multiple tasks and call `commit()` uniformly. Data visibility is only guaranteed after `drain()` returns.
- When `nranks == 2`, `prev` and `next` refer to the same peer rank, so seg1 and seg2 are validated against the same peer pattern.
- The current sample has established P2P channels in both the `prev` and `next` directions, but only executes data-plane operations in the `next` direction by default. To extend to
  full all-to-neighbor communication, reuse the established `prev` channel: launch the Kernel twice, first using the `prev` channel
  to execute `read_nbi` reading `prev`'s seg0 into local seg2, then using the `next` channel to execute `write_nbi` writing local seg0 into
  `next`'s seg1, so that each rank's seg1 and seg2 both match `prev`'s pattern. When extending, keep the Host-side barriers before and after Kernel execution;
  if data segment offsets are adjusted, update the expected rank and offset used by Host-side pattern validation accordingly.