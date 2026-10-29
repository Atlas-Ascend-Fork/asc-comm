# CCU Direct AllGather NHR1D Mem2Mem Sample

## Overview

This sample shows how to issue a CCU kernel with the direct-launch `<<<>>>` approach to implement NHR1D Mem2Mem AllGather.

The inputs of all ranks are concatenated in rank order, and each rank obtains a complete copy of the data.

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR&950DT series products | >= CANN 9.1.0 |

## Directory Structure

```text
03_allgather_nhr1d_mem2mem
├── CMakeLists.txt    // CMake build file
├── README.md         // Chinese sample description
├── README_en.md      // English sample description
├── run.sh            // One-step build-and-run script
├── main.asc          // Host flow and NHR1D Mem2Mem AllGather kernel implementation
└── scripts
    ├── gen_data.py       // Input and golden data generation script
    └── verify_result.py  // Output verification script
```

## Sample Description

### Sample Specification

| Item | Description |
| --- | --- |
| Communication mode | CCU NHR1D Mem2Mem AllGather in an HCCL communication domain |
| Invocation | Direct CCU kernel launch with `<<<>>>` |
| NHR steps | `ceil(log2(rankSize))` |
| Data type | FP32 |
| Per-rank input size | `COUNT` FP32 elements, 256 by default |
| Output size | `rankSize * COUNT` FP32 elements |
| Supported ranks | 1 to 16, no more than `CCU_MAX_RANK_SIZE` |

### Implementation Flow

1. Initialize ACL and the HCCL communication domain. The number of ranks is determined by the number of devices specified by `DEVICES`.
2. Each rank reads data from the input file and copies it to the device-side input buffer.
3. The host generates the NHR steps and the rank-to-channel mapping, and acquires CCU channel and CCU instance resources based on the HCCL communication domain.
4. Obtain the input memory token through `asccomm_ccu_get_mem_token` and prepare CCU task arguments.
5. Directly launch the CCU kernel through `CcuAllGatherNhr1DMem2MemKernel<<<schd, insHandle, stream>>>`.
6. Inside the kernel, first copy the local slice through `GroupCopy`; then write and forward slices remotely through `ccu::write` per NHR step, and synchronize with the peer through `notify` after each non-final step.
7. Synchronize the stream, copy the result back to the host, and print it.
8. Destroy the HCCL communication domain, stream, and device memory.

## Build and Run

Perform the following steps in the sample root directory to build and run the sample. This sample supports only the NPU run mode.

- Configure environment variables

  Configure the development environment according to how the CANN development kit package is installed, and enable CCU scheduling mode.

  ```bash
  source ${install_path}/cann/set_env.sh
  export HCCL_OP_EXPANSION_MODE=CCU_SCHED
  ```

  > **Note:** `${install_path}` is the CANN package installation directory. If no installation directory is specified, the package is installed in `/usr/local/Ascend` by default.

- Run the sample

  Generate the input and golden data. The data generation and verification scripts require Python 3 and NumPy.

  ```bash
  python3 scripts/gen_data.py --rank-size 2 --count 256
  ```

  Build and run the sample:

  ```bash
  env DEVICES=0,1 COUNT=256 ./run.sh
  ```

  Verify the output of each rank:

  ```bash
  python3 scripts/verify_result.py --rank-size 2 --count 256
  ```

  `--rank-size` must match the number of devices specified by `DEVICES`, and `--count` must match the `COUNT` used when running the sample.
  `run.sh` only builds and runs the sample and does not automatically generate or verify data. To build the sample only, run `env BUILD_ONLY=1 ./run.sh`.
  After each change to the rank count or `count`, regenerate the data and run and verify it with the same parameters.

- Build options

  | Option | Possible values | Description |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU architecture, corresponding to the Ascend 950PR&950DT series products |

- Expected result

  The following is partial output with 2 ranks and 256 FP32 elements per rank. The two-NPU case contains one communication step. The print order across ranks may vary with thread scheduling.

  ```text
  rank 1 NHR schedule: step0(to=0,from=0,tx=[1])
  rank 0 NHR schedule: step0(to=1,from=1,tx=[0])
  rank 1: launched, waiting for stream
  rank 0: launched, waiting for stream
  rank 0: stream completed
  rank 1: stream completed
  rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
  rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
  rank 0: output saved to <sample_path>/output/output_rank_0.bin
  rank 1: output saved to <sample_path>/output/output_rank_1.bin
  rank 0 verify pass
  rank 1 verify pass
  test pass!
  ```

  Every rank passes verification and prints `verify pass`, and `test pass!` is printed at the end, indicating that the sample ran successfully.

## Notes

- At least two NPUs are required to run the sample. A single-NPU environment supports compilation verification only.
- Set `HCCL_OP_EXPANSION_MODE=CCU_SCHED` before running the sample.
- This sample uses the `dav-3510` architecture by default and applies only to the Ascend 950PR&950DT series products.
