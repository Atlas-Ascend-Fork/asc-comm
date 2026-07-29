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

## Build Unit Tests
Use `-t` or `--test` to build Hcomm UTs.
```bash
bash build.sh -t
```

Default build directory:
```text
build/ut-hcomm
```

## Build a Development Run Package

After modifying Hcomm headers locally, build a lightweight development package to install the headers from the current worktree into an existing CANN environment without rebuilding the complete toolkit package:

```bash
bash build.sh --pkg
```

The generated file is placed in `build_out/` by default:

```text
Ascend-asccomm_1.0.0_linux-<arch>.run
```

`<arch>` is `aarch64` or `x86_64`. At packaging time, all `.h` files currently present under the following two directories are collected recursively. File names and the file count are not fixed, and subdirectory paths relative to each scan root are retained:

| Repository scan directory | CANN installation directory |
| --- | --- |
| `include/aicore/hcomm/` | `asc/include/adv_api/hcomm/` |
| `src/aicore/hcomm/detail/` | `asc/include/adv_api/hcomm/detail/` |
| `src/aicore/hcomm/detail/` | `asc/impl/adv_api/detail/hcomm/` |

For example, `include/aicore/hcomm/hcomm.h` is installed as `asc/include/adv_api/hcomm/hcomm.h`. `src/aicore/hcomm/detail/common/hcomm_base.h` is installed as both `asc/include/adv_api/hcomm/detail/common/hcomm_base.h` and `asc/impl/adv_api/detail/hcomm/common/hcomm_base.h`. This dual-directory layout matches the asc-devkit integration packaging rules for asc-comm: the former resolves `hcomm/detail/...` references in asc-comm public headers, while the latter preserves the existing CANN detail installation path.

Consequently, a new `.h` file under either scan directory is automatically included the next time the package is built, and each new detail header automatically produces two installation targets. File names, source file counts, and installed file counts are not fixed; other file types are excluded. The package manifest describes only headers present at packaging time and does not represent a complete mirror of the target directories. On a first installation, a path already deleted or renamed in the repository is absent from the manifest, so the installer cannot remove the corresponding old path from CANN. Do not rely solely on this run package to verify the final directory state of deletions or renames.

Common options are listed below:

| Option | Description |
| --- | --- |
| `--install` | Install or update all Hcomm headers in the package. |
| `--uninstall` | Restore original files from the baseline and remove files that were previously absent. |
| `--check` | Verify the run archive and payload without modifying CANN. |
| `--install-path=<PATH>` | Select an absolute path for installation or uninstallation; defaults to `ASCEND_HOME_PATH`. |
| `--force` | Override asc-devkit version or installed-patch file consistency checks. |
| `--list` | List files in the run package; provided by the makeself wrapper. |
| `-h`, `--help` | Display help information. |

`--install`, `--uninstall`, and `--check` are mutually exclusive operations; specify exactly one at a time.

### Inspect the Package

Before installation, verify the archive and payload checksums and print the package version, source commit, architecture, required asc-devkit version, and file count. This does not modify CANN:

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --check
```

List the files embedded in the run package:

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --list
```

### Install and Uninstall

Install into the current `ASCEND_HOME_PATH`:

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --install
```

Alternatively, select an installation location with an absolute path:

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run \
    --install --install-path=/path/to/cann
```

The installer tries the `--install-path` value itself, `<install-path>/cann`, and `<install-path>/ascend-toolkit/latest` as the CANN root. The resolved directory must contain `asc/include/adv_api/`, `asc/impl/adv_api/detail/`, and `share/info/asc-devkit/version.info`. The current user must own the resolved CANN root.

On the first installation, baseline state is stored under the resolved CANN root:

```text
var/asc-comm-dev-patch/baseline/
```

- If a target file already exists, the installer saves the original before replacement. Uninstallation restores the original file and its permissions.
- If a target file does not exist, the installer records it as `absent` before creating it. Uninstallation removes that file.
- Repeated installations into the same CANN directory retain the baseline from before each path was first managed by the patch. Files from a previous patch installation never overwrite that baseline.
- If a path from the previous asc-comm patch is absent from a new package, repeated installation restores that recorded path to its original state. The installer cannot handle an old CANN file that was never recorded by a patch and is also absent from the current package.

Restore the files to their pre-installation state. If `--install-path` was used for installation, pass the same path when uninstalling:

```bash
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --uninstall
# Corresponding command for an installation with an explicit path
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run \
    --uninstall --install-path=/path/to/cann
```

The package records the asc-devkit version and system architecture at build time. Installation requires a matching system architecture and, by default, the same asc-devkit version as the packaging environment. Before a repeated installation or uninstallation, the installer also verifies the currently installed patch files so that direct modifications under CANN are not silently overwritten or discarded.

Use `--force` only when the compatibility and overwrite risks are understood. During installation, it can override an asc-devkit version mismatch or modifications to installed patch files. During uninstallation, it can discard modifications to installed patch files and restore the baseline. `--force` cannot override the system architecture check or payload checksum verification.

After installation, recompile the target Ascend C kernel before verification. Existing `.o` and `.bin` files are not updated automatically.

To specify the package version, output directory, and CANN environment used for packaging, invoke the packaging script directly:

```bash
bash scripts/package/build_package.sh \
    --cann-path=/path/to/cann \
    --package-version=1.0.0 \
    --output-dir=/path/to/output
```

To verify installation, repeated installation, file protection, and uninstall restoration against a temporary CANN directory:

```bash
bash tests/package/test_run_package.sh \
    build_out/Ascend-asccomm_1.0.0_linux-<arch>.run
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
