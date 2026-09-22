# CCU Builtin AlltoAllvWrite Sample

## Overview

This sample demonstrates how to prepare CCU communication resources and start a CCU Server through the MC2
builtin APIs, and then submit an AlltoAllvWrite Client communication request from an AICore kernel through
the HCCL high-level API.

The sample creates one rank for each available NPU device. Each rank sends 256 FP32 elements to every
destination rank. AlltoAllvWrite describes send offsets and sizes in bytes and writes the data into the
destination rank's CCU scratch window. On the host, `Mc2AcquireCcResCtx` obtains the resource context and
`Mc2CcKernelLaunch` starts the CCU Server. On the AICore, the same `ccResCtx` is passed to `Hccl::InitV2`,
and `Hccl::AlltoAllvWrite` submits the communication request.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
07_all2allvwrite
├── CMakeLists.txt    // Build configuration file
├── README.md         // Chinese documentation
├── README_en.md      // English documentation
└── main.asc          // Host flow, CCU resource setup, and AICore communication Client implementation
```

## Sample Description

Each rank sends 256 FP32 elements to every destination rank. The input buffer is stored contiguously
by destination rank. The block sent from rank `i` to rank `j` is initialized to the small integer
`i * CCU_MAX_RANK_SIZE + j`. After AlltoAllvWrite, the destination rank's CCU scratch window
contains the blocks ordered by source rank, with the corresponding source/destination value. The
maximum value is `CCU_MAX_RANK_SIZE * CCU_MAX_RANK_SIZE - 1`, so switching to `int8_t` or `uint8_t`
does not truncate the payload.

AlltoAllvWrite uses bytes for offsets and sizes. Because the 950 CCU algorithm uses two dies, the sample
builds `sendSizes/sendOffsets` arrays with `2 * rankSize` entries and splits every destination block into
two die slices as required by the API.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AlltoAllvWrite<true> (submit CCU Client request)
  -> CCU scratch window
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AlltoAllvWrite in an HCCL communication domain |
| CCU Server launch | Host-side `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| Communication request | AICore-side `Hccl::AlltoAllvWrite<true>` |
| AICore Kernel | `all_to_all_v_write_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 payload, with communication metadata expressed as INT8 bytes |
| Data sent to each destination rank | 256 FP32 elements, or 1024 bytes |
| Output location | CCU scratch window pointed to by `ccResCtx.res[0]` |
| Algorithm configuration | `CcuHalfAll2AllVMesh1D` |
| Supported ranks | No more than `CCU_MAX_RANK_SIZE`, which is 8 in this sample |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPU devices.
2. Create one rank for each device, create the AICore stream, and allocate the input buffer and the GM buffer
   that stores `sendSizes/sendOffsets`.
3. Initialize the data blocks and fill the send sizes and byte offsets according to the 950 CCU dual-die layout.
4. Create an MC2 argument object through `Mc2GetCcArgs`, and set the `CCU_SCHED` communication engine, INT8
   byte data types, and the `CcuHalfAll2AllVMesh1D` algorithm configuration.
5. Call `Mc2AcquireCcResCtx` to obtain the CCU resource context `ccResCtx` and its size.
6. Release the MC2 argument object and call `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` to start the
   CCU Server. The `stream` parameter is reserved for the AICPU path and is currently ignored by the CCU path.
7. Launch the AICore kernel through `all_to_all_v_write_kernel<<<1, nullptr, streamAiv>>>`. Inside the kernel,
   call `hccl.InitV2(contextGM)`, where `contextGM` is the `ccResCtx` obtained on the host, and then call
   `Hccl::AlltoAllvWrite<true>`, `Wait`, and `SyncAll`.
8. Synchronize the AICore stream, copy `ccResCtx` to the host, obtain the scratch address from
   `OpResCtx.res[0]`, copy the scratch result to the host, and validate it by source rank.
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

In a two-device run, the first element of the block sent from rank `d` to rank `j` is
`d * CCU_MAX_RANK_SIZE + j`. Each rank prints send and receive summaries and `validation: PASS` on success.

## Notes

- At least two NPU devices are required to run the sample. Single-device environments support compilation only.
- Pass the same `ccResCtx` returned by `Mc2AcquireCcResCtx` to both `Mc2CcKernelLaunch` and the AICore
  `all_to_all_v_write_kernel`.
- Before running, verify that the selected `mc2_client` supports `HCCL_CMD_HALF_ALLTOALLV`, the
  `CcuHalfAll2AllVMesh1D` algorithm, and its CCU executor. If the devkit only defines the enum without
  completing these registrations, `Mc2AcquireCcResCtx` returns an unsupported-operation error.
- `AlltoAllvWrite` expresses `sendSizes/sendOffsets` and `remoteWinOffset/localDataSize` in bytes.
- The sample reads `res[0]` from the 950 CCU MC2 `HcclApi::OpResCtx` to validate the CCU scratch window.
  This is not the `GetWindowsOutAddr` interface.
- The sample uses `dav-3510` by default and covers Ascend 950PR/Ascend 950DT.
- The sample uses all visible NPU devices, and the device count must not exceed `CCU_MAX_RANK_SIZE` (8).
