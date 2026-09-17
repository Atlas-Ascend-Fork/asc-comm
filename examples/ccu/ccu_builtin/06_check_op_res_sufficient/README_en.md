# CheckOpResSufficient API Example

## Overview

This example focuses on how to call the `CheckOpResSufficient` API: before a CCU builtin communication operation, it builds arguments consistent with the later communication and checks whether the required resources are sufficient. When the check passes, the example continues with an AllGather flow to demonstrate the complete usage after the API call; AllGather itself is not the focus of this example.

If the check fails, for example with 1043 for insufficient resources, no communication is issued and the caller decides how to handle the result; this example exits with an error. `CheckOpResSufficient` does not consume CCU resources and has no side effects.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
06_check_op_res_sufficient
├── CMakeLists.txt    // build script
├── README.md         // example description (Chinese)
├── README_en.md      // example description (English)
└── main.asc          // host-side check flow, CCU resource preparation and AICore client
```

## Example Description

### Function

Each rank inputs `sendCount` FP32 elements. The host first builds the MC2 argument object
identical to the real communication and calls `CheckOpResSufficient` to probe resources; on
success it acquires the CCU resource context, starts the CCU Server, and the AICore
`all_gather_kernel` aggregates each rank's input into every rank's `recvBuf` in rank-id order
via `Hccl::AllGather`. The host then verifies the result element by element.

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc* (CCU_SCHED + FP32 + CcuSchedAllGatherSoleMesh)
  -> CheckOpResSufficient   (checkOnly probe; continue only when sufficient)
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch      (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AllGather (submit CCU Client request)
  -> recvBuf
```

### Specifications

| Item | Description |
| --- | --- |
| Communication mode | CCU AllGather within an HCCL communication domain |
| Resource probe | Host-side `CheckOpResSufficient(comm, ccType, ccArgs)` with the same arguments as the later Acquire |
| CCU Server start | Host-side `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| Client submission | AICore-side `Hccl::AllGather<true>` |
| AICore kernel | `all_gather_kernel<<<1, nullptr, streamAiv>>>` |
| Data type | FP32 |
| Per-rank input size | 256 FP32 elements |
| AllGather output size | `rankSize * 256` FP32 elements |
| Result check | element-wise: segment `rank` of `recvBuf` equals `rank+1` |
| Supported ranks | up to `CCU_MAX_RANK_SIZE` (16 in this example) |

### Flow

1. Initialize ACL and the HCCL communication domain, and query the number of NPUs.
2. Create one rank per device, an AICore stream, and input/recv buffers.
3. Build the MC2 argument object via `Mc2GetCcArgs`, setting the `CCU_SCHED` engine, FP32
   source/destination data types, and the `CcuSchedAllGatherSoleMesh` algorithm.
4. Call `CheckOpResSufficient(comm, HCCL_CMD_ALLGATHER, ccArgs)`: `HCCL_SUCCESS` means
   resources are sufficient and the flow continues; a non-zero return (e.g. 1043) skips the
   communication and returns the error directly.
5. After the check passes, acquire the CCU resource context `ccResCtx` via `Mc2AcquireCcResCtx`,
   free the MC2 argument object, and start the CCU Server via
   `Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)`.
6. Launch the AICore kernel via `all_gather_kernel<<<1, nullptr, streamAiv>>>`; the kernel calls
   `hccl.InitV2(contextGM)` where `contextGM` is exactly the host-side `ccResCtx`.
7. The AICore side submits the client task via `Hccl::AllGather<true>`, then `Wait`/`SyncAll`.
8. Synchronize the stream, copy `recvBuf` back to host, verify element by element (segment
   `rank` equals `rank+1`) and print the result.
9. Destroy the communication domain, stream, and device buffers. `ccResCtx` is owned by the
   communication domain and needs no separate free.

## Build and Run

- Set up the environment

  ```bash
  source ${install_path}/cann/set_env.sh
  export HCCL_OP_EXPANSION_MODE="CCU_SCHED"
  ```

- Build and run

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./demo
  ```

- Sample output

  On 2 cards, rank `d` initializes its input elements to `d + 1`. A successful run prints:

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  CheckOpResSufficient ret=0, resource sufficient, start AllGather
  CheckOpResSufficient ret=0, resource sufficient, start AllGather
  rankId: 0, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
  rankId: 1, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
  rankId: 0, result check: PASS
  rankId: 1, result check: PASS
  ```

  Each rank prints the check-succeeded message, then the aggregated `recvBuf` and the PASS
  verdict, indicating the example ran successfully.

## Notes

- The example requires at least 2 NPUs.
- `CheckOpResSufficient` and `Mc2AcquireCcResCtx` must use identical arguments; otherwise the
  check result is meaningless for the real communication.
- The `ccResCtx` returned by `Mc2AcquireCcResCtx` is passed to both `Mc2CcKernelLaunch` and the
  AICore `all_gather_kernel`; they must use the same resource context.
- AICore client tasks must be submitted after the CCU Server starts; `Hccl::AllGather` is not a
  host-side AllGather.
- The example defaults to the `dav-3510` architecture, covering Ascend 950PR/Ascend 950DT only.
- The example uses all visible NPU devices, up to `CCU_MAX_RANK_SIZE`.
