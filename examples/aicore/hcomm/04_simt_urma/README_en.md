# Hcomm SIMT URMA Functional Sample

## Overview

This sample validates all SIMT URMA point-to-point communication interfaces in `hcomm/hcomm_simt.h`: `WriteNbi`, `WriteValueNbi`,
`WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS`, and covers both immediate and deferred publication.

The topology is a ring: each rank sends operations to the memory registered by `next = (rank + 1) % nranks` and validates
the results written by `prev = (rank - 1 + nranks) % nranks`. Each rank is both a sender and a receiver, and both sides are checked:
`sendBuf` stores the old values fetched back by this rank's atomic operations, and `recvBuf` stores the data written by `prev`. Each `recvBuf` on the ring has exactly
one writer, so all expected values are deterministic.

All operations are issued by a single SIMT thread. One channel must be driven by one lane: a deferred WQE stays in the send queue,
and a later `commit=true` post publishes a producer index covering the whole batch, so all tasks in the same batch must be posted by the same lane.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.3.0 |

## Directory Structure

```text
simt_urma
├── CMakeLists.txt          // Build project file
├── main.cpp                // Host-side communicator, memory registration, and verification flow
├── op_kernel.cpp           // SIMT Kernel-side URMA interface calls
├── simt_urma_common.h      // Definitions shared by Host and Kernel
├── README.md               // Chinese sample documentation
└── README_en.md            // English sample documentation
```

## Sample Description

### Functionality Description

The sample provides ten test modes, covering immediate and deferred publication for each of the five interfaces:

**Write family**

`WriteNbi` takes data from a local buffer, and the WQE carries an SGE pointing to that buffer. Slot `i` carries the value `i + 1`, so on the receiving side
any wrong value can be traced directly to the slot it should have come from.

`WriteValueNbi` inlines the value to be written in the WQE without reading the send buffer. It requires `config` to have inline enabled (the default is
`URMA_INLINE_CFG`) and `sizeof(T)` not to exceed the WQE inline payload area; both are checked at compile time.

| `mode` | Publication |
| --- | --- |
| `write_single` | 32 independent `WriteNbi<commit=true>` calls |
| `write_batch_last` | 31 serial `WriteNbi<false>` calls, then one `WriteNbi<true>` |
| `write_value_single` | 32 independent `WriteValueNbi<commit=true>` calls |
| `write_value_batch_last` | 31 serial `WriteValueNbi<false>` calls, then one `WriteValueNbi<true>` |

**Notify/Atomic family**

| `mode` | Description |
| --- | --- |
| `notify` | Submits one Notify operation and calls `Drain` |
| `faa` | Submits one FAA operation and validates the remote accumulated result and the old value fetched back locally |
| `cas` | Submits one CAS operation and validates the remote swapped result and the old value fetched back locally |
| `single` (default) | Submits Notify, FAA, and CAS serially, calling `Drain` after each operation |
| `batch_last` | Defers Notify and FAA with `commit=false`, then publishes the whole batch with a `commit=true` CAS |
| `notify_immediate_repeat` | One lane submits `kNotifyImmediateRepeatCount` `WriteWithNotifyNbi<true>` posts in a row (currently 100), then calls `Drain` once at the end |

The two families use non-overlapping slot layouts, and the buffer is allocated according to the larger of the two. SIMT has no `Commit` interface, so a deferred
task can only be carried out by a later `commit=true` post. All modes call `Drain` at the end to ensure that the CQ is consumed before the kernel
returns.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Ring topology SIMT URMA point-to-point communication |
| Invocation Method | Direct URMA interface calls within the SIMT Kernel |
| Test Modes | `write_single`, `write_batch_last`, `write_value_single`, `write_value_batch_last`, `notify`, `faa`, `cas`, `single`, `batch_last`, `notify_immediate_repeat` |
| Supported Rank Count | Any >= 2 |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   the ranks exchange root info through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`)
   and create the communicator.
2. Each rank registers `sendBuf` and `recvBuf` through `HcclCommMemReg`.
3. Obtains the link endpoint to `next` through `HcclRankGraphGetLayers`/`HcclRankGraphGetLinks`, then calls
   `HcclChannelAcquire` to create the channel and obtains the peer memory address through `HcclChannelGetRemoteMems`.
4. The Kernel side submits the URMA operations according to the selected mode, then calls `Drain` to wait for completion.
5. The Host side uses two barriers (`ready` and `sent`) over the control channel to ensure that all ranks finish registration and writing before reading back and validating.
6. Validates the values fetched back in `sendBuf` and the data landed in `recvBuf`, and outputs PASS/FAIL.

## Compilation and Execution

Perform the following steps in the root directory of this sample to compile and run it. This sample supports NPU run mode only.

- Configure environment variables

  The sample builds against the asc-comm headers installed into the CANN directory. If they are not yet installed, first generate and install the development
  verification run package from the repository root. For parameter details, see [`docs/en/compilation/build_and_test.md`](../../../../docs/en/compilation/build_and_test.md):

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_9.3.0_linux-<arch>.run --full
  ```

  Then load the CANN environment variables:

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If no installation directory is specified, the default is `/usr/local/Ascend`.

- Run the sample

  Execute the following commands in the sample directory. The executable internally forks all rank processes, so it can be run directly:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/simt_urma tcp://127.0.0.1:29620 <nranks> [mode]
  ```

- Launch parameter description

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of ranks in the communicator; must be an integer not less than 2 |
  | `[mode]` | `single` | Test mode; for the allowed values, see Sample Description; an unrecognized mode is rejected directly instead of silently falling back |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT products |

- Expected output

  Taking a two-card run of `write_single` as an example, each rank prints one set of results:

  ```text
  [rank 0] simt_urma write_single | sent to rank 1, received from rank 1 | PASS
  [rank 1] simt_urma write_single | sent to rank 0, received from rank 0 | PASS
  RESULT | Example=simt_urma Mode=write_single | Status=PASS
  ```

  The sample succeeds when all ranks print `PASS` and the final `Status=PASS` is printed.

## Notes

- Running the sample requires at least two NPUs; a single-card environment supports compilation verification only.
- When the Kernel side resolves buffer base addresses through `AscendC::simt::LocalBufferAddr` and `RemoteBufferAddr`, what is passed is an
  index into the channel buffer table, not an index into the `memHandles` array: local table index 0 is reserved by the CCL buffer, and the first explicitly
  registered buffer uses index 1. The remote buffer index is looked up by tag through `HcclChannelGetRemoteMems`. Passing a `memHandles` index
  directly resolves to the CCL buffer, in which case the CQE returns normally but the receiver sees the CCL buffer content instead of the payload.
- The Host-side `sent` barrier is the key to trustworthy validation results: without it, a rank may read a zeroed buffer while the writer's kernel has not
  finished.
- The executable captures `SIGINT`/`SIGTERM` and terminates all rank processes, so Ctrl+C does not leave processes blocked on a Host barrier and
  still holding devices.
- The build relies on the CANN ASC CMake utilities and, at the link stage, on the CANN `hcomm` library.
