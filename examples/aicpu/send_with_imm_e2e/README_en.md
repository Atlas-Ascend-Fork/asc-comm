# AICPU Send With Immediate End-to-End Sample

## Overview

This sample verifies the complete two-sided communication path of the AICPU dataplane API
`asccomm_send_with_imm_on_thread`. A custom AICPU kernel on the client submits a SEND with immediate data,
while the server creates a host URMA channel, registers receive memory, pre-posts receive buffers, and validates
receive completions and payloads through `HcommChannelPollJfc`.

The sample checks every message for receive completion, `ok`, `isRecv`, `hasImmData`, `immData`, `byteLen`,
and the payload byte sequence.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
send_with_imm_e2e
├── CMakeLists.txt              // Build configuration
├── README.md                   // Chinese documentation
├── README_en.md                // English documentation
├── common/send_with_imm_e2e.h  // Test protocol and parameters
├── client/main.cpp             // Client resource setup and AICPU kernel launch
├── server/main.cpp             // Server receive resource management and CQE validation
├── kernel/send_with_imm_kernel.cc // Custom AICPU communication kernel
├── scripts/run.sh              // One-command build and run script
└── send_with_imm_aicpu.json.in // AICPU kernel metadata template
```

## Sample Description

### Functionality

1. The server queries a host endpoint, creates the TCP control connection, and creates a `COMM_ENGINE_CPU` channel.
2. The server allocates host receive memory, registers it through `HcommMemReg`, pre-posts receive buffers with
   `HcommChannelPostRecv`, and sends the memory descriptor to the client.
3. The client queries a device endpoint, imports the server memory descriptor, creates an `COMM_ENGINE_AICPU_TS`
   channel, and allocates and registers device send memory.
4. The client obtains an AICPU-usable channel handle with `HcommChannelBuildDeviceEntity`, loads
   `send_with_imm_aicpu.json` through the ACL runtime, and launches the custom AICPU kernel.
5. The kernel sends data with `asccomm_send_with_imm_on_thread(thread, channel, src, len, immData)`.
6. The server waits for receive completions with `HcommChannelPollJfc`, validates CQE fields and payload, and
   returns the statistics to the client.

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | AICPU-to-host URMA two-sided SEND |
| AICPU API | `asccomm_send_with_imm_on_thread` |
| Engines | Client `AICPU_TS`, server `CPU` |
| Message count | `SEND_COUNT`, default 1 |
| Message size | `PAYLOAD_BYTES`, default 64 bytes |
| Immediate data | `IMM_DATA`, default 305419896 (0x12345678) |
| Receive slots | `RECV_SLOTS`, default 8 |

## Build and Run

### Prerequisites

- Source the CANN environment:

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

- The CANN environment must contain the latest hcomm package with `HcommChannelPostRecv`, `HcommChannelPollJfc`,
  and `HcommChannelBuildDeviceEntity`, plus the asc-comm development package with the AICPU headers and
  `libasccomm_aicpu.so`. Install the latest hcomm package first; if the asc-comm package is absent, build and
  install it from the asc-comm repository root:

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_<version>_linux-<arch>.run --full
  ```

### Run

Run from the sample directory:

```bash
bash scripts/run.sh
```

The script builds into `build/`, then starts the server and client. To reuse an existing build and change only
runtime parameters, run:

```bash
bash scripts/run.sh --skip-build
```

For cross-host execution, start the server on the server host and run the script on the client host with
`CLIENT_HOST` set to the server address. Both sides must use the same TCP port and test parameters.

### Parameters

| Environment variable | Default | Description |
| --- | ---: | --- |
| `SERVER_TCP_PORT` | 49152 | TCP control port |
| `SERVER_HOST_IP` | Auto-selected | IPv4 address used by the host endpoint. Specify it explicitly on multi-NIC hosts |
| `CLIENT_HOST` | 127.0.0.1 | Server IP address |
| `DEVICE_ID` | 0 | NPU device used by the client |
| `PAYLOAD_BYTES` | 64 | Payload size in bytes |
| `IMM_DATA` | 305419896 | 32-bit immediate data |
| `SEND_COUNT` | 1 | Number of messages |
| `RECV_SLOTS` | 8 | Pre-posted receive slots |
| `LOG_FILE` | `build/send_with_imm_run_<timestamp>.log` | Run log file |
| `BUILD_JOBS` | nproc | Build job count |

### Expected Output

A successful run prints output similar to:

```text
[send-imm-server] listening on port 49152
[send-imm-server] received=1 errors=0 expected=1
[send-imm-client] received=1 errors=0 apiReturn=0 expected=1
```

The sample passes when both the client and server exit with code 0.

## Notes

- This sample verifies a real AICPU/URMA data path. Environments without an NPU can verify compilation only.
- `build/send_with_imm_aicpu.json` records the absolute path of the custom AICPU SO during CMake configuration.
  Reconfigure the project after moving the build directory.
- The runtime environment must allow the ACL runtime to load the custom AICPU shared library by that path.
- The run script saves stdout and stderr to `LOG_FILE`. When a run fails, check the last printed stage and
  return code in that file first.
- `SEND_COUNT` can be greater than 1; the server polls repeatedly and reuses completed slots.
- The communication channel uses fixed port 60001. Avoid port conflicts when running multiple instances on the
  same pair of hosts.

