# Third-party Dependencies & Compatibility

## Scope

The in-repo build of asc-comm is mainly used for environment checks, AICore Hcomm API UT verification and Hcomm sample verification. This document describes the direct dependencies currently involved in the in-repo build, verification and sample workflows.

## Base Environment

| Dependency | Requirement | Description |
| --- | --- | --- |
| CANN Toolkit | Matching the current branch or tag | Before running `build.sh`, you must run `source ${install_path}/cann/set_env.sh` first. The script checks `ASCEND_HOME_PATH`. |
| CANN Runtime/HCCL/Hcomm | CANN 9.1.0 or later | The `hcomm_write_read_nbi` sample requires communication domain creation, memory registration and AIV P2P channel creation capabilities, and depends on the host-side `hcomm` library at the linking stage. |
| CMake | >= 3.16 | The CMake entry of UTs is `tests/ut/CMakeLists.txt`. |
| C++ compiler | C++17 supported | UT targets use `CMAKE_CXX_STANDARD 17`. It is recommended to use `gcc/g++ >= 7.3.0` with consistent versions. |
| Python | Python 3 | UTs can be used to generate tiling header files; OAT hooks require Python 3.7+. Python >= 3.9.0 is recommended for the source code and examples environments. |

## Direct Third-party Dependencies of This Repository

| Scenario | Dependency | Version | How to Obtain or Configure |
| --- | --- | --- | --- |
| UT | googletest | 1.14.0 | Prefer the system GTest. If no system GTest is available, point `CANN_3RD_LIB_PATH` to the CANN third_party directory. |
| Samples | CANN ASC CMake capability and hcomm library | CANN 9.1.0 or later | After running `source ${install_path}/cann/set_env.sh`, build with CMake in the `examples/hcomm_write_read_nbi` directory. |
| Code formatting | clang-format | v18.1.8 | `pre-commit-config.yaml` pulls from `pre-commit-clang/mirrors-clang-format`. |
| Open-source compliance check | oat-py | >= 1.0.1 | `scripts/oat_check.sh` attempts automatic installation; if it fails, manually run `pip install oat-py>=1.0.1`. |

The `Third_Party_Open_Source_Software_List.yaml` in the repository root directory currently registers only `googletest`, which is directly used by in-repo testing. If new third-party libraries that are directly linked or packaged are added later, this list and the Notice must be updated synchronously.

## Sample Runtime Dependencies

The `examples/hcomm_write_read_nbi` sample supports Ascend 950PR/Ascend 950DT and requires at least two NPUs at runtime. A single-card environment can complete build verification but cannot perform the two-card point-to-point communication run verification. The sample uses `COMM_ENGINE_AIV` and `COMM_PROTOCOL_UB_CTP` only and does not cover the RoCE path.

The sample build commands are as follows:

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/hcomm_write_read_nbi
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## GTest Installation and Configuration

If GTest is not installed on the system, prepare the following directory structure:

```text
<third_party>/gtest/include/gtest/gtest.h
<third_party>/gtest/lib64/libgtest.a
```

Then run:

```bash
source /usr/local/Ascend/cann/set_env.sh
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<third_party>
cmake --build build/ut-hcomm
```

`build.sh -t` triggers the UT build. To specify an offline GTest path, you can directly use the preceding CMake commands, or pass it in through the build script:

```bash
bash build.sh -t --cann_3rd_lib_path=<third_party>
```

## Integration Dependency Boundary

When new modules, samples or end-to-end workflows introduce new third-party components later, this section, the third-party open-source software list and the Notice of this repository must be updated synchronously.
