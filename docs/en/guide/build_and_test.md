# Build & Test

## Environment Preparation
Before executing `build.sh`, source the CANN environment script to set environment variables (`build.sh` checks `ASCEND_HOME_PATH`, which must be configured whether building UTs or not).

```bash
# Default installation path (root user example; replace /usr/local with ${HOME} for non-root users)
source /usr/local/Ascend/cann/set_env.sh
# Custom installation path
# source ${install_path}/cann/set_env.sh
```

Refer to [Third-party Dependencies & Compatibility](./dependencies.md) for the list of basic environment and third-party dependencies.

## Build Instructions
When running the build script directly, it will perform basic environment checks. UT building must be triggered separately via dedicated build arguments.
```bash
bash build.sh
```

The default build reuses the existing `build/` directory and does not remove build artifacts. Run `bash build.sh --make_clean` explicitly when a clean build directory is required.

## Build Unit Tests
Use `-t` or `--test` to build Hcomm UTs.
```bash
bash build.sh -t
```

Default build directory:
```text
build/ut-hcomm
```

## CMake Entry
The CMake entry for unit tests is:
```text
tests/ut/CMakeLists.txt
```

Common CMake variables:
| Variable | Description |
| --- | --- |
| `ASCEND_CANN_PACKAGE_PATH` | Path to the CANN package. If not explicitly specified, it is derived from environment variables by priority. |
| `PRODUCT_TYPE_LIST` | List of product types to build. Defaults to `ascend950pr_9599_AIV` and `ascend910B1_AIC`. |
| `TEST_MOD` | Filter for UT targets to execute. Defaults to `all`. |
| `ASCCOMM_UT_RUN_AFTER_BUILD` | Whether to run UTs after compilation. Defaults to `ON`. Set to `OFF` for compilation only. |

## GTest Dependency
UTs will search for system-installed GTest first. If GTest is unavailable locally, point `CANN_3RD_LIB_PATH` to the CANN third-party directory.
```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<path-to-third-party>
```

You may also pass the CANN third-party directory via the build script:
```bash
bash build.sh -t --cann_3rd_lib_path=<path-to-third-party>
```

## Build and Run Samples
`examples/hcomm_write_read_nbi` provides a point-to-point communication sample using AIV direct-driven URMA `WriteNbi` and `ReadNbi`. This sample adopts an independent CMake project for building:
```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/hcomm_write_read_nbi
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

The sample can launch two ranks directly by default:
```bash
./demo
```

You can also specify ranks manually:
```bash
# Terminal 1: rank 0
./demo 0 2 tcp://127.0.0.1:29621

# Terminal 2: rank 1
./demo 1 2 tcp://127.0.0.1:29621
```

The sample supports Ascend 950PR / Ascend 950DT and requires CANN 9.1.0 or later. At least two NPUs are required to run the sample; single-NPU environments only support compilation verification.
