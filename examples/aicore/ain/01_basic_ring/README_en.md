# AIN Basic Ring Sample

## Overview

This sample demonstrates how to use the basic AIN communication capabilities through the `ain/ain.h` interface in an Ascend C AIV Kernel. The sample runs in a symmetric multi-rank
manner: each rank issues `Put` to the next rank, issues `Get` from the previous rank, and finally synchronizes through
`AinBarrierSession`.

The Host side allocates memory, initializes the HCCL communicator, creates the HCCL Team, registers symmetric windows, and creates AIV communication channels;
the Kernel side only submits data-plane communication tasks through the AIN interface.

## Supported Products and CANN Software Versions

| Product | CANN Software Version |
| --- | --- |
| Ascend 950PR&950DT products | >= CANN 9.1.0 |

## Directory Structure

```text
basic_ring
├── CMakeLists.txt    // Build project file
├── basic_ring.asc    // Host-side resource setup, AICore-side AIN calls, and shared definitions
├── README.md         // Chinese sample documentation
└── README_en.md      // English sample documentation
```

## Sample Description

### Functionality Description

`nranks` ranks form a ring topology. The current rank `r` performs two one-sided communication operations:

- `Put`: writes its own rank id to `recvWindow[rankId]` on the next rank (`(r + 1) % nranks`)
- `Get`: reads data from `sendWindow[0]` on the previous rank (`(r - 1 + nranks) % nranks`) into the local
  `recvWindow[rankNum + rankId]`

After the communication completes, each rank reads back `recvBuf` and validates the `Put` and `Get` results.

### Sample Specifications

| Item | Description |
| --- | --- |
| Communication Mode | Ring topology AIN point-to-point one-sided communication |
| Communication Engine | `COMM_ENGINE_AIV` with the `COMM_PROTOCOL_UB_CTP` protocol |
| Deployment Mode | Single-node multi-card |
| Supported Rank Count | >= 2, not exceeding the number of available NPUs |

### Implementation Flow

1. The executable directly launches all rank processes (in-process fork, see `examples/aicore/utils/process_manager.h`);
   the ranks exchange root info through the TCP control channel specified on the command line (`examples/aicore/utils/rank_sync.h`) and
   create the multi-rank HCCL communicator.
2. Each rank initializes the ACL runtime environment, configures the team descriptor through `HcclTeamCreateDescInit` (including `barrierCount`,
   `rankIds`, the `COMM_ENGINE_AIV` communication engine, the `COMM_PROTOCOL_UB_CTP` protocol, and `channelCnt`),
   and calls `HcclTeamCreate` to create the team, which automatically establishes the AIV + UB_CTP communication channels.
3. Registers `sendBuf` and `recvBuf` as symmetric windows through `HcclCommSymWinRegister`.
4. Passes `team`, `sendWin`, and `recvWin` to the Kernel.
5. The Kernel side constructs an `AscendC::Ain` object and initializes the Hcomm UB temporary workspace, performs `Put` and `Get`, then calls `Flush`
   to wait for the communication tasks to complete, and finally synchronizes the ranks through `AinBarrierSession::Sync`.

## Compilation and Execution

Perform the following steps in the root directory of this sample to compile and run it. This sample supports NPU run mode only.

- Configure environment variables

  Configure the environment variables according to the installation method of the CANN development kit on your current environment.

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If not specified, the default installation directory is `/usr/local/Ascend`.

- Run the sample

  Execute the following commands in the sample directory. The executable internally forks all rank processes, so it can be run directly:

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/basic_ring_demo tcp://127.0.0.1:29622 <nranks>
  ```

- Launch parameter description

  | Parameter | Default | Description |
  | --- | --- | --- |
  | `<ip:port>` | None | Control channel address (`tcp://ip:port` format) that rank 0 listens on, used for root info exchange; use different ports when running multiple instances in parallel |
  | `<nranks>` | None | Number of ranks to launch; must be an integer not less than 2 and not exceeding the number of available NPUs |

- Build options description

  | Option | Allowed Values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu` (default) | Run mode; this sample supports NPU execution only |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to Ascend 950PR&950DT products |

- Expected output

  Each rank reads back `recvBuf` and prints the `Put` and `Get` results, for example:

  ```text
  [rank 0] basic_ring | PUT recvBuf[1]=1, GET recvBuf[2]=1 | PASS
  [rank 1] basic_ring | PUT recvBuf[0]=0, GET recvBuf[3]=0 | PASS
  RESULT | Example=basic_ring | Status=PASS
  ```

  The sample succeeds when all ranks print `PASS` and the final `Status=PASS` is printed.

## Notes

- Running the sample requires at least two NPUs; a single-card environment supports compilation verification only.
- Load the CANN environment variables before compiling and running to ensure that `ASCEND_CANN_PACKAGE_PATH`, the ASC CMake modules, and the runtime
  shared libraries are available.
- The sample depends on the CANN `hccl`, `hcomm`, `ascendcl`, and `runtime` libraries.
- The number of ranks must not exceed the number of available NPUs at runtime.
- The sample supports single-node multi-card execution only and does not support cross-node multi-node execution.
