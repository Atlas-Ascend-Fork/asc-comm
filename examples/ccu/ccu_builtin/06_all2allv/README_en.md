# CCU Builtin AlltoAllV Sample

## Overview

This sample demonstrates how to prepare CCU communication resources and start a CCU Server through the MC2
builtin APIs, and then submit an AlltoAllV Client communication request from an AICore kernel through the
HCCL high-level API.

The sample creates one rank for each available NPU device. Each rank sends a different number of FP32
elements to every destination rank. On the host, `Mc2AcquireCcResCtx` obtains the CCU resource context and
`Mc2CcKernelLaunch` starts the CCU Server. On the AICore, the same `ccResCtx` is passed to `Hccl::InitV2`,
and `Hccl::AlltoAllV` performs the variable-size data exchange.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
06_all2allv
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, CCU resource setup, and AICore communication Client implementation
```

## Sample Description

Each rank sends `256 + local rank + destination rank` FP32 elements to each destination rank.
The blocks are stored contiguously in the input buffer by destination rank. The block sent from rank `i`
to rank `j` is filled with the small integer `i * CCU_MAX_RANK_SIZE + j`. After AlltoAllV, rank `j`
validates the block received from rank `i` using the same value and variable length. The maximum value
is `CCU_MAX_RANK_SIZE * CCU_MAX_RANK_SIZE - 1`, so switching to `int8_t` or `uint8_t` does not
truncate the payload.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AlltoAllV<true> (submit CCU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AlltoAllV in an HCCL communication domain |
| CCU Server launch | Host-side `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AlltoAllV<true>` |
| AICore kernel | `all_to_all_v_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Data sent to each destination rank | `256 + local rank + destination rank` FP32 elements |
| Input/output size | The sum of the per-peer variable data lengths |
| Algorithm configuration | `CcuSchedAllToAllVSoleMesh` |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICore stream, and allocate input and output buffers based on
   the per-peer data lengths.
3. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `CCU_SCHED` communication engine, FP32
   source and destination data types, and the `CcuSchedAllToAllVSoleMesh` algorithm configuration.
4. Call `Mc2AcquireCcResCtx` to obtain the CCU resource context `ccResCtx` and its size.
5. Release the MC2 argument object and call `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` to start the
   CCU Server. The `stream` parameter is reserved for the AICPU path and is currently ignored by the CCU path.
6. Launch the AICore kernel through `all_to_all_v_kernel<<<1, nullptr, streamAiv>>>`. Inside the kernel,
   `hccl.InitV2(contextGM, nullptr)` is called, where `contextGM` is the `ccResCtx` obtained on the host.
7. The AICore side builds the `sendCounts/recvCounts` and `sdispls/rdispls` arrays for its rank, calls
   `Hccl::AlltoAllV<true>`, and calls `Wait` and `SyncAll` to wait for completion.
8. Synchronize the AICore stream, copy `recvBuf` back to the host, and validate every variable-size block by
   source rank.
9. Destroy the HCCL communication domain, stream, and device memory. `ccResCtx` is owned by the communication
   domain and does not need to be freed separately.

## Build and Run

Run the following commands in this sample directory. This sample supports NPU execution only.

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./demo
```

`${install_path}` is the CANN installation directory. The default is `/usr/local/Ascend`.

In a two-device run, rank `d` sends a block of length `256 + d + j` to destination rank `j`.
Every element in the block is `d * CCU_MAX_RANK_SIZE + j`. Every rank prints a receive-block summary
and `validation: PASS` on success.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `all_to_all_v_kernel`.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE` (8).
- AlltoAllV counts and displacements are expressed in elements, not bytes.
