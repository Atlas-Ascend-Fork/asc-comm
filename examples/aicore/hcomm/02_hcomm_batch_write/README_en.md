# Hcomm Batch Write Sample

## Overview

This sample demonstrates how to use the Hcomm batch write API to write data from rank 0 to the other ranks in a single-host multi-NPU environment. Multiple URMA channels share one Jetty and submit cross-peer write tasks in batches through `MakeBatchHandle`, `GetHandleRef`, `BatchCommit`, and `Drain`. The program prepares the data to be sent, submits a batch of write operations, and waits for completion. The receiving ranks then verify the received data, and the parent process finally aggregates the results of all ranks.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
hcomm_batch_write
├── CMakeLists.txt               // CMake build file
├── hcomm_batch_write.asc        // Host resource setup and Kernel invocation
├── hcomm_batch_write_kernel.cpp // Batch-write calls from the AIV Kernel
├── hcomm_batch_write_def.h      // Definitions shared by Host and Kernel
├── README.md                    // Chinese sample documentation
└── README_en.md                 // English sample documentation
```

## Sample Description

### Functionality Description

Rank 0 writes a local data segment to the receive buffers of all other ranks at the same time through the batch write API. Batch submission packages multiple write tasks into one batch handle and issues them through a single `BatchCommit`, reducing the number of submissions. After `Drain` waits for the whole batch to complete, each receiving rank verifies that the written data is consistent with the data sent by rank 0.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | One-to-many batch write from rank 0 to all other ranks |
| Invocation Method | `MakeBatchHandle`/`GetHandleRef`/`BatchCommit`/`Drain` within the AIV Kernel |
| Data Validation | Each receiving rank verifies the written data |
| Supported Rank Count | 2 to 16, not exceeding the number of available NPUs on the host |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`). Ranks exchange root info through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`) and create the communication domain, and rank 0 prepares the data to be sent.
2. Each rank registers the communication memory and exchanges buffer addresses through the control channel. Rank 0 establishes a URMA channel that shares the Jetty with each peer.
3. The Kernel side constructs a batch handle, fills in a write task reference for each peer, and then issues a single `BatchCommit`.
4. `Drain` waits for the whole batch of tasks to complete.
5. Each receiving rank verifies the written data, and the parent process aggregates the results of all ranks.

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
  cd .. && ./build/hcomm_batch_write tcp://127.0.0.1:29624 <rank_num>
  ```

- Launch parameter description

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange and Host-side barriers; use different ports when running multiple instances in parallel |
  | `<rank_num>` | None | Number of ranks to launch, ranging from 2 to 16, and must not exceed the number of available NPUs on the host |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Execution mode; this sample only supports NPU execution |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT products |

- Expected output

  After a successful four-rank run, each receiving rank prints a verification-passed message, and the parent process finally outputs:

  ```text
  [rank 1] hcomm_batch_write | received and verified data from rank 0 | PASS
  [rank 2] hcomm_batch_write | received and verified data from rank 0 | PASS
  [rank 3] hcomm_batch_write | received and verified data from rank 0 | PASS
  RESULT | Example=hcomm_batch_write | Status=PASS
  ```

  When the parent process prints `Status=FAIL` or any rank reports an error, the batch write or data verification has not completed successfully.

## Notes

- Running the sample requires at least 2 NPUs; a single-card environment only supports compilation verification.
- The rank count must not exceed the number of available NPUs on the host, and the running user must have device access permission.
- Compilation depends on the CANN ASC CMake capability and links the CANN `hcomm` library at the link stage.
