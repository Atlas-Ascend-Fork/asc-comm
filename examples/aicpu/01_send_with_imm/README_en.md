# AICPU TS `asccomm_send_with_imm_on_thread` Real-Device Example

## Overview

This example validates the new asc-comm AICPU data-plane API `asccomm_send_with_imm_on_thread`
on the **AICPU TS direct-drive** path. It uses a two-card (or multi-card) ring topology: each rank
sets up a `COMM_PROTOCOL_UB_CTP` channel to the next rank, the host prepares the communication
domain, registers memory and acquires channel/thread resources, then downloads a case table to the
device. An AICPU kernel calls the API under test case by case, and the host reads the results back
and asserts them field by field.

Unlike tests that only check the return code, this example reads the WQE back from the real UB SQ
memory and asserts the hardware descriptor content directly. On real hardware it therefore confirms:

- the API uses the `UDMA_OPC_SEND_WITH_IMM` opcode instead of a plain `WRITE`;
- the 32-bit immediate lands in the `immData` union field of `UdmaSqeCommon` that aliases `rmtAddr`;
- the local SGE address, length, token and slice count follow the `process_slices` slicing rule;
- `pi` advances by the slice count and the RTSQ doorbell is rung;
- parameter and protocol rejection paths emit no WQE at all.

## Supported Products And CANN Versions

| Product | CANN Version |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.2.0 |

## Directory Structure

```text
01_send_with_imm
├── CMakeLists.txt              // Build project file
├── send_with_imm.asc           // Host-side resource setup, kernel launch and assertions
├── send_with_imm_kernel.aicpu  // AICPU kernel: API invocation and WQE readback
├── send_with_imm_def.h         // Definitions shared by host and kernel (case table, WQE decoding)
├── README.md                   // Chinese readme
└── README_en.md                // English readme
```

## Description

### Function

API prototype under test (see `include/aicpu/hcomm/asccomm_primitives.h`):

```c
int32_t asccomm_send_with_imm_on_thread(
    ThreadHandle thread, ChannelHandle channel, const void* src, uint64_t len, uint32_t immData);
```

| Item | Description |
|------|------|
| Semantics | Two-sided send carrying a 32-bit immediate; the peer receives the immediate through a special CQE |
| Protocols | UB protocols only (`UB_CTP`/`UBC_TP`/`UB_MEM` and alike); others return `ASCCOMM_E_NOT_SUPPORT` |
| Underlying opcode | `UDMA_OPC_SEND_WITH_IMM` |
| Remote address | Not required; the `rmtAddr` slot of the SQE carries `immData` instead |

### Case Matrix

The kernel runs the following 8 cases in order and the host asserts each of them:

| # | Case | Type | len | immData | Expected | Guards |
|---|--------|------|-----|---------|-----------|--------|
| 0 | `positive/typical` | positive | 256 | `0xA5A5A5A5` | `SUCCESS` | Typical value, single slice, all WQE fields |
| 1 | `positive/min-len-imm-low` | positive | 1 | `0x00000000` | `SUCCESS` | Minimum length + immData lower bound |
| 2 | `positive/imm-high` | positive | 256 | `0xFFFFFFFF` | `SUCCESS` | immData upper bound |
| 3 | `positive/multi-slice` | positive | 1 MB | `0x5A5A5A5A` | `SUCCESS` | Slicing above `maxWriteSize` and cumulative `pi` |
| 4 | `zero-length` | boundary | 0 | `0x12345678` | `SUCCESS` | No WQE emitted, `pi` unchanged |
| 5 | `null-src` | negative | 256 | 0 | `E_PTR` | Null `src` rejection |
| 6 | `bad-thread-magic` | negative | 256 | 0 | `E_PTR` | `ThreadEntity` ABI header magic-word check |
| 7 | `non-ub-protocol` | negative | 256 | 0 | `E_NOT_SUPPORT` | Non-UB protocol rejection |

Negative cases copy the real entity on device and corrupt a single field (magic word / protocol),
so the real resources stay intact.

### Assertions

For positive cases with `len > 0`, all of the following are asserted on real hardware:

| Assertion | Expected |
|------|--------|
| `apiRet` | `ASCCOMM_SUCCESS` |
| `opcode` | `UDMA_OPC_SEND_WITH_IMM` (1) |
| `immDataLow` | the `immData` of the case |
| `immDataHigh` | 0 |
| `inlineEn` | 0 |
| `sgeNum` | 1 |
| `sgeAddr` | local source address |
| `sgeLength` | `min(len, maxWriteSize)`, i.e. the first slice length |
| `cqe` | 1 for a single slice, 0 for the first slice of a multi-slice transfer |
| `piAfter` | `(piBefore + sliceNum) & 0xFFFF` |

Negative cases assert that `apiRet` equals the expected error code and that
`piAfter == piBefore` (no WQE emitted).

### Flow

1. The executable forks all rank processes (`examples/aicore/utils/process_manager.h`). Ranks
   exchange root info and create the communication domain over the TCP control channel given on the
   command line (`examples/aicore/utils/rank_sync.h`).
2. Each rank allocates a 4 MB device communication buffer and registers it with `HcclCommMemReg`;
   the first half is the send source, the second half is reserved for observing peer landing data.
3. `HcclRankGraphGetLayers`/`HcclRankGraphGetLinks` locate the `COMM_PROTOCOL_UB_CTP` link to
   `(rank + 1) % nranks`, and `HcclChannelAcquire` acquires the channel with `COMM_ENGINE_AICPU_TS`.
4. `HcclThreadAcquireWithStream` acquires thread resources bound to the AICPU task stream, again
   with `COMM_ENGINE_AICPU_TS`.
5. The host reads back the `CommAbiHeader` pointed to by each handle and verifies that it is a
   POD v2 `ThreadEntity`/`ChannelEntity`; a control-plane version mismatch fails fast here with an
   explicit message.
6. The host fills the case table, copies it into the device-side `KernelContext` and launches the
   AICPU kernel with `<<<1, 0, stream>>>`.
7. The kernel calls `asccomm_send_with_imm_on_thread` for each case, records the return code and
   reads back the 64-byte WQE at `pi` from
   `ChannelEntity.sqContextAddr[0].contextInfo.ubJfs.sqVa`.
8. The host synchronizes the stream with a timeout, reads the `KernelContext` back, asserts every
   field and prints PASS/FAIL per case.
9. After a control-channel barrier, each rank observes its own receive region and prints the
   `PEER_PAYLOAD` diagnostic line.

## Build And Run

Run the following steps in this example directory. NPU run mode only — **this example cannot run in
a CPU-only environment without an NPU**.

- Set up the environment

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN installation directory, `/usr/local/Ascend` by default.

- Prerequisites

  | Dependency | Description |
  |------|------|
  | ASC / AICPU CMake packages | Provide `find_package(ASC)`, `find_package(AICPU)` and the `<<<>>>` AICPU direct-launch capability |
  | `libhcomm.so` / `libhccl.so` | Control-plane APIs: communication domain, memory registration, channel and thread resources |
  | `libasccomm_aicpu.so` | asc-comm AICPU data plane, provides the API under test `asccomm_send_with_imm_on_thread` |
  | POD v2 control plane | The control plane must produce `ThreadEntity`/`ChannelEntity` POD handles; see `include/aicpu/hcomm/hcomm_res_entity_defs.h` |

- Run the example

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && timeout --signal=TERM --kill-after=10s 300s \
      ./build/asccomm_send_with_imm tcp://127.0.0.1:29631 <nranks>
  ```

- Arguments

  | Argument | Default | Description |
  |------|--------|------|
  | `<ip:port>` | none | Control-channel address listened on by rank 0 (`tcp://ip:port`); use distinct ports for parallel instances |
  | `<nranks>` | none | Number of ranks to launch, must be >= 2 and no more than the available NPU count |

- Build options

  | Option | Values | Description |
  |------|--------|------|
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` (default) | NPU architecture, maps to Ascend 950PR/Ascend 950DT |

- Expected output

  On a successful two-card run the terminal prints something like:

  ```text
  [rank 0] case[positive/typical] apiRet=0 expectRet=0 pi 0->1 sliceNum=1 | PASS
  [rank 0] case[positive/min-len-imm-low] apiRet=0 expectRet=0 pi 1->2 sliceNum=1 | PASS
  [rank 0] case[positive/imm-high] apiRet=0 expectRet=0 pi 2->3 sliceNum=1 | PASS
  [rank 0] case[positive/multi-slice] apiRet=0 expectRet=0 pi 3->4 sliceNum=1 | PASS
  [rank 0] case[zero-length] apiRet=0 expectRet=0 pi 4->4 sliceNum=0 | PASS
  [rank 0] case[null-src] apiRet=2 expectRet=2 pi 4->4 sliceNum=0 | PASS
  [rank 0] case[bad-thread-magic] apiRet=2 expectRet=2 pi 4->4 sliceNum=0 | PASS
  [rank 0] case[non-ub-protocol] apiRet=5 expectRet=5 pi 4->4 sliceNum=0 | PASS
  [rank 0] PEER_PAYLOAD | from rank 1 | Status=NOT_LANDED
  [rank 0] asccomm_send_with_imm_on_thread | peer rank 1 | PASS
  RESULT | Example=asccomm_send_with_imm | Status=PASS
  ```

  Pass criteria: process exit code 0 and `Status=PASS` on the `RESULT` line.

## Notes

- At least 2 NPUs are required to run; a single-card environment supports build verification only.
- The API under test can only execute on the AICPU: ringing the RTSQ doorbell depends on
  `halSqCqQuery`/`halSqCqConfig` and on `GetSqeId` provided by the device runtime, so a CPU-only
  environment cannot run it.
- `PEER_PAYLOAD` is a **non-gating diagnostic**. `SEND_WITH_IMM` is two-sided: the payload and the
  immediate are delivered through the peer receive CQE, and the asc-comm data plane does not expose
  a recv/poll CQE API yet, so the immediate cannot be verified on the receive side. `NOT_LANDED` is
  therefore expected and does not affect the `RESULT` verdict. Once a receive API is available, this
  item can be promoted to a gating assertion.
- The actual slice count of `positive/multi-slice` depends on the `maxWriteSize` published by the
  control plane. The example computes the expectation from the `maxWriteSize` read back on device,
  so `sliceNum` may be 1 or larger depending on the environment; both are normal.
- Kernel synchronization uses `aclrtSynchronizeStreamWithTimeout` (60 s by default). If the
  underlying link hangs, the example exits with FAIL instead of blocking forever; keep the outer
  `timeout --kill-after` guard anyway and probe device health with `npu-smi info` after a hard kill.
- The example launches `nranks` ranks over all visible NPUs; rank `r` sends to rank `(r + 1) % nranks`.
- 4 of the 8 cases really transmit on the wire. If peer receive resources are tight, reduce the
  number of positive cases in `CASE_TABLE` and rebuild.
