# Third-party Dependencies & Compatibility

## Scope

The in-repository build of asc-comm is mainly used for environment verification, AICore Hcomm interface UT validation and Hcomm sample validation. This document describes the direct dependencies integrated into the repository build, verification and sample workflows.

## Basic Environment

| Dependency | Requirement | Description |
| --- | --- | --- |
| CANN Toolkit | Matches the current branch or tag | `source ${install_path}/cann/set_env.sh` must be executed before running `build.sh`. The script checks `ASCEND_HOME_PATH`. |
| CANN Runtime / HCCL / Hcomm | CANN 9.1.0 or later | The `hcomm_write_read_nbi` sample requires capabilities for communication domain creation, memory registration and AIV P2P channel creation, and depends on the Host-side `hcomm` library at link time. |
| CMake | >= 3.16 | UT CMake entry: `tests/ut/CMakeLists.txt`. |
| C++ Compiler | C++17 support | UT targets adopt `CMAKE_CXX_STANDARD 17`. It is recommended to use `gcc/g++ >= 7.3.0` with consistent versions. |
| Python | Python 3 | UT can generate tiling header files; OAT hooks require Python 3.7+. Python >= 3.9.0 is recommended for source and examples environments. |

## Direct Third-party Dependencies of This Repository

| Scenario | Dependency | Version | Acquisition & Configuration |
| --- | --- | --- | --- |
| UT | googletest | 1.14.0 | System GTest is preferred. If unavailable, point `CANN_3RD_LIB_PATH` to the CANN third_party directory. |
| Samples | CANN ASC CMake utilities and hcomm library | CANN 9.1.0 or later | After running `source ${install_path}/cann/set_env.sh`, build via CMake under `examples/aicore/hcomm/01_hcomm_write_read_nbi`. |
| Code Formatting | clang-format | v18.1.8 | Pulled from `pre-commit-clang/mirrors-clang-format` defined in `pre-commit-config.yaml`. |
| Open Source Compliance Check | oat-py | >= 1.0.1 | `scripts/oat_check.sh` attempts automatic installation. Run `pip install oat-py>=1.0.1` manually upon failure. |

`Third_Party_Open_Source_Software_List.yaml` at repository root currently only records `googletest` used directly for testing. If new third-party libraries for linking or packaging are introduced later, this inventory and Notice file shall be updated synchronously.

## Sample Runtime Dependencies

The `examples/aicore/hcomm/01_hcomm_write_read_nbi` sample supports Ascend 950PR / Ascend 950DT and requires at least two NPUs for runtime. Compilation verification can be completed on single-NPU environments, whereas two-card point-to-point communication runtime verification is unavailable.
This sample uses `COMM_ENGINE_AIV` and `COMM_PROTOCOL_UBC_CTP` exclusively and does not cover the RoCE path.

Sample build commands:

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/01_hcomm_write_read_nbi
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## GTest Installation & Configuration

If GTest is not installed on the system, prepare the following directory layout:

```text
<third_party>/gtest/include/gtest/gtest.h
<third_party>/gtest/lib64/libgtest.a
```

Then execute:

```bash
source /usr/local/Ascend/cann/set_env.sh
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<third_party>
cmake --build build/ut-hcomm
```

`build.sh -t` triggers UT building. To specify an offline GTest path, you can either use the above CMake command directly or pass the path via the build script:

```bash
bash build.sh -t --cann_3rd_lib_path=<third_party>
```

## Integration Dependency Boundary

When new third-party components are introduced by newly added modules, samples or end-to-end workflows in the future, this section, the repository third-party open source inventory and Notice shall be updated synchronously.
