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

After modifying Hcomm or Ain headers locally, build a lightweight development package to install the relevant headers from the current worktree into an existing CANN environment without rebuilding the complete toolkit package:

```bash
bash build.sh --pkg
```

The generated file is placed in `build_out/` by default:

```text
cann-asc-comm_1.0.0_linux-<arch>.run
```

`<arch>` is `aarch64` or `x86_64`. At packaging time, every `.h` file under the following directories is collected recursively:

| Repository scan directory | CANN installation directory |
| --- | --- |
| `include/aicore/hcomm/` | `asc/include/adv_api/hcomm/` |
| `src/aicore/hcomm/` | `asc/impl/adv_api/detail/hcomm/` |
| `include/aicore/ain/` | `asc/include/comm_api/aicore/ain/` |
| `src/aicore/ain/` | `asc/impl/comm_api/aicore/ain/` |

This mapping matches the current packaging rules in asc-devkit's `cmake/third_party/asc-comm.cmake`, and relative subdirectories within each source directory are preserved. Installation also creates these Hcomm symbolic links:

```text
asc/include/comm_api/aicore/hcomm -> ../../adv_api/hcomm
asc/impl/comm_api/aicore/hcomm    -> ../../adv_api/detail/hcomm
```

Consequently, a new `.h` file under any scan directory is automatically included the next time the package is built. File names, source file counts, and installed file counts are not fixed; other file types are excluded. The package manifest describes only headers present at packaging time and does not represent a complete mirror of the target directories. On a first installation, a path already deleted or renamed in the repository is absent from the manifest, so the installer cannot remove the corresponding old path from CANN. Do not rely solely on this run package to verify the final directory state of deletions or renames.

Common options are listed below:

| Option | Description |
| --- | --- |
| `--full` | Install or update all packaged Hcomm and Ain headers and Hcomm links in full mode. |
| `--uninstall` | Restore original files from the baseline and remove files that were previously absent. |
| `--check` | Verify the run archive, payload, and dependency compatibility without modifying CANN. |
| `--install-path=<PATH>` | Select the Ascend installation root; defaults to `/usr/local/Ascend` for root or `$HOME/Ascend` for non-root users. |
| `--force` | Override installed-patch file consistency checks. |
| `--list` | List files in the run package; provided by the makeself wrapper. |
| `-h`, `--help` | Display help information. |

`--full`, `--uninstall`, and `--check` are mutually exclusive operations; specify exactly one at a time.

### Inspect the Package

Before installation, verify the archive and payload checksums and print the package version, source commit, architecture, required asc-devkit, hcomm, and runtime versions, and file count. This does not modify CANN:

```bash
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --check
```

List the files embedded in the run package:

```bash
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --list
```

### Install and Uninstall

Install into the default Ascend root:

```bash
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --full
```

Alternatively, select an installation location with an absolute path:

```bash
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run \
    --full --install-path=/path/to/cann
```

The installer tries the `--install-path` value itself, `<install-path>/cann`, and `<install-path>/ascend-toolkit/latest` as the CANN root. The resolved directory must contain `asc/include/adv_api/`, `asc/impl/adv_api/detail/`, and `share/info/`. The current user must own the resolved CANN root.

On the first installation, baseline state is stored under the resolved CANN root:

```text
var/asc-comm-dev-patch/baseline/
```

- If a target file already exists, the installer saves the original before replacement. Uninstallation restores the original file and its permissions.
- If a target file does not exist, the installer records it as `absent` before creating it. Uninstallation removes that file.
- For a non-root installation, AICore Hcomm and Ain files use mode `550`, Hcomm directories use mode `750`, and managed directories under `comm_api` use mode `550`. Consistent with the devkit and hcomm packages, a root installation automatically enables access for all users and changes these modes to `555`, `755`, and `555`, respectively. The installer records original directory permissions; reinstallation, uninstallation, or failed-operation rollback restores them without recursively changing unrelated directories.
- The installer records whether each Hcomm link existed before the first installation. Uninstallation preserves a pre-existing correct link and removes a link created by this package. A wrong-target link, regular file, or directory occupying either link path is never overwritten.
- On repeated installation into the same CANN directory, the installer first restores the previous patch and then rebuilds the baseline for the new package from the restored CANN tree. Files from the previous patch are never saved as originals.
- If a path from the previous asc-comm patch is absent from a new package, repeated installation restores that path and stops managing it; later uninstallation does not touch it. The installer cannot handle an old CANN file that was never recorded by a patch and is also absent from the current package.

Restore the files to their pre-installation state. If `--install-path` was used for installation, pass the same path when uninstalling:

```bash
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --uninstall
# Corresponding command for an installation with an explicit path
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run \
    --uninstall --install-path=/path/to/cann
```

The package records the major and minor versions of asc-devkit, hcomm, and runtime, as well as the system architecture, at build time. Installation requires a matching system architecture. Incompatible dependency versions follow the CANN package convention: a warning is emitted and installation continues. Before a repeated installation or uninstallation, the installer also verifies the currently installed patch files so that direct modifications under CANN are not silently overwritten or discarded.

Use `--force` only when the overwrite risk is understood. During installation, it can override modifications to installed patch files. During uninstallation, it can discard modifications to installed patch files and restore the baseline. `--force` cannot override the system architecture check or payload checksum verification.

Install, upgrade, and uninstall operations write logs using the CANN package convention. Root writes under `/var/log/ascend_seclog`, while non-root users write under `$HOME/var/log/ascend_seclog`. Process logs are appended to `ascend_install.log`, and operation audit records are appended to `operation.log`.

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
    build_out/cann-asc-comm_1.0.0_linux-<arch>.run
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
