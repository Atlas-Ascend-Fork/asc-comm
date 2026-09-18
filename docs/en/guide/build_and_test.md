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

## Build a Development Run Package

After modifying asc-comm headers locally, build a lightweight development package to install the relevant headers from the current worktree into an existing CANN environment without rebuilding the complete toolkit package:

```bash
bash build.sh --pkg
```

The generated file is placed in `build_out/` by default:

```text
cann-asc-comm_9.2.0_linux-<arch>.run
```

`<arch>` is `aarch64` or `x86_64`. At packaging time, every `.h` file under the following directories is collected recursively:

| Repository scan directory | CANN installation directory |
| --- | --- |
| `include/aicore/hcomm/` | `asc/include/adv_api/hcomm/` |
| `src/aicore/hcomm/` | `asc/impl/adv_api/detail/hcomm/` |
| Other directories under `include/` | `asc/include/comm_api/`, preserving relative paths |
| Other directories under `src/` | `asc/impl/comm_api/`, preserving relative paths |

The installation paths follow the asc-devkit subpackage layout. Relative subdirectories within each source directory are preserved. Installation also creates these Hcomm symbolic links:

```text
asc/include/comm_api/aicore/hcomm -> ../../adv_api/hcomm
asc/impl/comm_api/aicore/hcomm    -> ../../adv_api/detail/hcomm
```

For example, `include/aicore/ain/` is installed under `asc/include/comm_api/aicore/ain/`, and `include/ccu/` is installed under `asc/include/comm_api/ccu/`. The run package collects only `.h` files that exist under these directories at packaging time; other file types such as `.cpp` and `.inc` are not included. New headers are included automatically the next time the package is built.

> Note: The run package does not remove files from the target CANN installation when they are absent from its manifest. Before validating a header deletion or rename, remove the corresponding old file from the target CANN installation.

Common options are listed below:

| Option | Description |
| --- | --- |
| `--full` | Install or update all packaged asc-comm headers and Hcomm links in full mode. |
| `--uninstall` | Restore original files from the baseline and remove files that were previously absent. |
| `--check` | Verify the run archive, payload, and dependency compatibility without modifying CANN. |
| `--install-path=<PATH>` | Select the Ascend installation root; defaults to `/usr/local/Ascend` for root or `$HOME/Ascend` for non-root users. |
| `--install-for-all` | Allow all users to read and access packaged files during installation; enabled by default for root. |
| `--force` | Discard user modifications to package-managed files during installation or uninstallation. |
| `--quiet` | Run quietly for scripted use. |
| `--list` | List files in the run package; provided by the makeself wrapper. |
| `-h`, `--help` | Display help information. |

`--full`, `--uninstall`, and `--check` are mutually exclusive operations; specify exactly one at a time.

### Inspect the Package

Before installation, verify the archive and payload checksums and print the package version, source commit, architecture, required asc-devkit, hcomm, and runtime versions, and file count. This does not modify CANN:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --check
```

List the files embedded in the run package:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --list
```

### Install

Install into the default Ascend root:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --full
```

Alternatively, select an installation location with an absolute path:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --install-path=/path/to/cann
```

The installer tries the `--install-path` value itself, `<install-path>/cann`, and `<install-path>/ascend-toolkit/latest` as the CANN root. The resolved directory must contain `asc/include/adv_api/`, `asc/impl/adv_api/detail/`, and `share/info/`. The current user must own the resolved CANN root.

For a non-root installation that needs to be available to other users, run:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --install-for-all --install-path=/path/to/cann
```

- Permission behavior follows the asc-devkit subpackage. A root installation sets header files to mode `555`, Hcomm directories to mode `755`, and managed directories under `comm_api` to mode `555`.
- A non-root installation uses modes `550`, `750`, and `550` by default. With `--install-for-all`, it uses `555`, `755`, and `555`. The target CANN root and its parent directories must allow other users to read and traverse them; otherwise, the installer refuses to proceed.
- The installer saves existing managed files and valid Hcomm links. It does not overwrite an incorrect link target or a regular file or directory occupying a link path.
- The package records the major and minor versions of asc-devkit, hcomm, and runtime, as well as the system architecture. The architecture must match. An incompatible dependency version produces a warning and installation continues.

### Reinstall

Install a new run package into the same CANN directory with `--full` to update it. Files from before the first installation remain available for later uninstallation.

If a package-managed file or link has been modified manually, reinstallation refuses to overwrite it. To discard those modifications, run:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --force --install-path=/path/to/cann
```

`--force` cannot bypass the architecture or payload checks. It also cannot overwrite an incorrect link target or a regular file or directory occupying a link path.

### Uninstall

Restore the files to their pre-installation state. If `--install-path` was used for installation, pass the same path when uninstalling:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --uninstall
# Corresponding command for an installation with an explicit path
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --uninstall --install-path=/path/to/cann
```

Uninstallation restores files and permissions that existed before installation and removes files and links created by the run package. If a package-managed file or link has been modified manually, uninstallation refuses to proceed. Add `--force` to the uninstall command to discard those modifications.

Installation, reinstallation, and uninstallation operations write logs. Root writes under `/var/log/ascend_seclog`, while non-root users write under `$HOME/var/log/ascend_seclog`. Process logs are appended to `ascend_install.log`, and operation audit records are appended to `operation.log`.

After installation, recompile the target Ascend C kernel before verification. Existing `.o` and `.bin` files are not updated automatically.

The run package version is read from `version.cmake` in the repository root. To specify the output directory and CANN environment used for packaging, invoke the packaging script directly:

```bash
bash scripts/package/build_package.sh \
    --cann_path=/path/to/cann \
    --output-dir=/path/to/output
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

`examples/aicore/hcomm/01_hcomm_write_read_nbi` provides a point-to-point communication sample using AIV direct-driven URMA `WriteNbi` and `ReadNbi`. This sample adopts an independent CMake project for building:

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/01_hcomm_write_read_nbi
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
