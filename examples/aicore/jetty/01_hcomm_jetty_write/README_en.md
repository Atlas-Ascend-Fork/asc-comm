# Hcomm Jetty Write Sample

## Overview

This sample demonstrates how to submit WQEs directly to the Jetty SQ from an AIV Kernel to perform point-to-point one-sided communication between multiple NPUs. The sample adopts a symmetric multi-rank execution model: each rank calls `HcommJetty::Write` and `HcommJetty::WriteValue` once for each of the other ranks, then waits for the whole batch of WQEs with `HcommJetty::Drain`. The Host side prepares the communication domain, registers memory, and creates the URMA channels; it does not need to participate in each data-plane operation. Each rank then verifies the data and markers it received, and the parent process reports the overall result.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
01_hcomm_jetty_write
├── CMakeLists.txt          // CMake build file
├── hcomm_jetty_write.asc   // Host resource setup, AIV Kernel-side Jetty calls, and shared definitions
├── README.md               // Chinese sample documentation
└── README_en.md            // English sample documentation
```

## Sample Description

### Functionality Description

Each rank writes a block of local data into the receive slot of every other rank, and writes an identifier value to a separate marker address. On the Kernel side, `HcommPeer` and `HcommJetty` build and submit WQEs directly: `Write` moves 256 bytes of data, `WriteValue` writes an 8-byte marker, and `Drain` waits for all WQEs submitted by this rank. After all ranks' writes land, each rank checks that the data slot and marker from every peer match the agreed values.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Many-to-many point-to-point communication in which all ranks write to each other |
| Communication Protocol | `COMM_PROTOCOL_UBC_CTP` |
| Communication Engine | `COMM_ENGINE_AIV` |
| Invocation Method | `HcommJetty::Write`/`WriteValue`/`Drain` within an AIV Kernel |
| Data Verification | Each rank verifies data slots and WriteValue markers from all peers |
| Supported Rank Count | 2 to 16, not exceeding the number of available NPUs |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`); ranks exchange root info and create the communication domain through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`).
2. Each rank prepares the data to send, registers the communication memory, builds a `COMM_PROTOCOL_UBC_CTP` channel description for every peer, and creates the channels.
3. Each rank writes all Jetty handles into a device-side table and downloads a per-peer context: the local buffer address, the remote data-slot address, the remote marker address, and the marker value.
4. After all channels are established, the kernel is launched; on the Kernel side it calls `Write` and `WriteValue` for each peer in turn, and waits for completion with `Drain`.
5. Each rank reads back the communication buffer, verifies the data slots and markers written by all peers, and the parent process reports the overall result.

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
  cd .. && ./build/hcomm_jetty_write tcp://127.0.0.1:29627 <rank_num>
  ```

- Launch parameter description

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<rank_num>` | None | Number of ranks to launch; must be in the range `2-16` and not exceed the number of available NPUs |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR/Ascend 950DT |

- Expected output

  After a successful two-card run, each rank prints a verification message and the parent process finally outputs:

  ```text
  [rank 0] hcomm_jetty_write | Write and WriteValue verified for 1 peer(s) | PASS
  [rank 1] hcomm_jetty_write | Write and WriteValue verified for 1 peer(s) | PASS
  RESULT | Example=hcomm_jetty_write | Status=PASS
  ```

  A `Status=FAIL` in the parent-process output or an error from any rank means that this Jetty write or data verification did not pass.

## Notes

- Running the sample requires at least 2 NPUs; a single-card environment only supports compilation verification.
- The rank count must not exceed the number of available NPUs, and the running user must have device access permission.
- The sample only supports single-node multi-card execution; cross-node multi-machine execution is not supported.
- Compilation depends on the CANN ASC CMake capability and links against the CANN `hcomm` library.
