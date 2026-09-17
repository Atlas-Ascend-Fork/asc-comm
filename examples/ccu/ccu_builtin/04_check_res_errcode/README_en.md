# CCU Builtin CheckOpResSufficient Error Code Verification Example

## Overview

This example is adapted from [01_allgather](../01_allgather) and is dedicated to verifying the error code
behavior of `CheckOpResSufficient(comm, ccType, ccArgs)` (the ccArgs variant), for functional comparison
with the mc2Tiling variant.

The example runs a set of error code cases on each rank, and rank 0 prints a summary of case name,
expected value, actual value, and verdict. The cases follow the ccArgs-variant trigger conditions in
`HcclAllocCcResByArgsImpl` (see `hccl_mc2.cc`).

## Supported Products and CANN Versions

| Product | CANN Version |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## Directory Structure

```text
04_check_res_errcode
├── CMakeLists.txt    // build script
├── README.md         // example description (Chinese)
├── README_en.md      // example description (English)
└── main.asc          // host-side error code case driver
```

## Case List

| Case | Construction | Expected Code | Trigger |
| --- | --- | --- | --- |
| NullComm | pass `comm=nullptr`, others valid | 2 (HCCL_E_PTR) | entry `CHK_PTR_NULL` |
| NullCcArgs | pass `ccArgs=nullptr`, others valid | 2 (HCCL_E_PTR) | entry `CHK_PTR_NULL` |
| SetEngine=AIV | `Mc2SetCcCommEngine(args, 3)` | 5 (HCCL_E_NOT_SUPPORT) | Set API engine whitelist |
| SetSrcDataType=RESERVED | `Mc2SetCcSrcDataType(args, 255)` | 5 (HCCL_E_NOT_SUPPORT) | `HcomCheckDataType` |
| UnsupportedOpType=GATHER | `ccType=HCCL_CMD_GATHER`, empty algConfig | 5 (HCCL_E_NOT_SUPPORT) | opType whitelist |
| UnregisteredAlg | `algConfig="NoSuchAlgName"` | 1042 (HCCL_E_ALG_NOT_SUPPORTED) | `CheckCcuAlgorithmsRegistered` |
| AicpuTsEngine | `commEngine=AICPU_TS(2)` | 0 (HCCL_SUCCESS) | checkOnly early return |
| LegacyAlgConfig | `algConfig="level_graph=0"` | 1042 (HCCL_E_ALG_NOT_SUPPORTED) | `CheckCcuAlgorithmsRegistered` (requires a plain alg name) |
| ResourceSufficient | 2 ranks + ALLGATHER + FP32 + registered alg | 0 (HCCL_SUCCESS) | resource-sufficient baseline |
| SingleRankGuard | single-card environment (devCount==1) | 0 (HCCL_SUCCESS) | rankSize==1 guard |
| ReuseInsInsufficient | enabled by `--stress-reuse` | observe (no verdict) | resource-chain UNAVAIL translation |

Notes:

- On a **single-card** environment, multi-rank cases are skipped automatically; only the single-rank guard
  and parameter cases run.
- In the ccArgs variant, most invalid parameters (engine/data type) are intercepted by the `Mc2SetCc*`
  setters up front, whereas the mc2Tiling variant intercepts them inside the check call - this difference
  in interception point is one of the focuses of the two-variant comparison.
- **1043 (HCCL_E_RES_NOT_SUFFICIENT)** can only appear when CCU resources are already occupied by other
  comms/instances on the device: checkOnly registers no kernels and holds no resources, so repeated checks
  in one process are idempotent and cannot create that situation. Therefore `./demo --stress-reuse` only
  observes once and asserts nothing. The process exit code is 0 when all cases pass, otherwise non-zero.

## Build and Run

- Set up the environment

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

- Build and run

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./demo                 # basic error code cases
  ./demo --stress-reuse  # plus the 1043 observation case (idle 950 required)
  ```

## Notes

- Multi-rank cases require at least 2 NPUs; a single-card environment runs only the single-rank guard and
  parameter cases.
- The 1043 observation case is affected by the global device resource state. Run it on an idle device and
  make sure historical comms have been released via `HcclCommDestroy`.
- The `ccResCtx` returned by `Mc2AcquireCcResCtx` is owned by the communication domain and is released
  with `HcclCommDestroy`; do not free it separately.
