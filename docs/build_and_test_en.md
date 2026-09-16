# Build and Test

## Environment Preparation

Before running `build.sh`, you need to source the CANN environment script to set environment variables (`build.sh` checks `ASCEND_HOME_PATH`; this is required whether or not UTs are built).

```bash
# Default installation path, root user as an example (for non-root users, replace /usr/local with ${HOME})
source /usr/local/Ascend/cann/set_env.sh
# Custom installation path
# source ${install_path}/cann/set_env.sh
```

For the base environment and third-party dependency list, see [Third-party Dependencies & Compatibility](./dependencies_en.md).

## Build Instructions

When the build script is executed directly, it completes the basic environment check. UTs need to be triggered separately through the test build option.

```bash
bash build.sh
```

The default build reuses the existing `build/` directory and does not automatically clean build artifacts. To clean the build directory, explicitly run `bash build.sh --make_clean`.

## Building UTs

Use `-t` or `--test` to build Hcomm UTs.

```bash
bash build.sh -t
```

The default build directory is:

```text
build/ut-hcomm
```

## Building the Development Verification run Package

After modifying asc-comm header files locally, you can generate a lightweight development verification package that installs the related header files from the current working tree into an existing CANN environment, without regenerating a full toolkit package:

```bash
bash build.sh --pkg
```

The generated file is located in `build_out/` by default:

```text
cann-asc-comm_9.2.0_linux-<arch>.run
```

Where `<arch>` is `aarch64` or `x86_64`. During packaging, all `.h` files in the following directories are scanned recursively:

| Repository Scan Directory | CANN Installation Directory |
| --- | --- |
| `include/aicore/hcomm/` | `asc/include/adv_api/hcomm/` |
| `src/aicore/hcomm/` | `asc/impl/adv_api/detail/hcomm/` |
| Other directories in `include/` | `asc/include/comm_api/`, relative paths preserved |
| Other directories in `src/` | `asc/impl/comm_api/`, relative paths preserved |

The installation directories follow the asc-devkit sub-package layout, and the relative directory structure within each scan directory is preserved. During installation, the following Hcomm symbolic links are also created:

```text
asc/include/comm_api/aicore/hcomm -> ../../adv_api/hcomm
asc/impl/comm_api/aicore/hcomm    -> ../../adv_api/detail/hcomm
```

For example, `include/aicore/ain/` is installed to `asc/include/comm_api/aicore/ain/`, and `include/ccu/` is installed to `asc/include/comm_api/ccu/`. The run package only collects `.h` files existing in the preceding directories at packaging time; other file types such as `.cpp` and `.inc` do not go into the package. After adding new header files in these directories and repackaging, the new files automatically go into the run package.

> Note: The run package does not clean files in the target CANN that are not listed in the package manifest. When verifying header file deletion or renaming, you need to first clean the corresponding old files in the target CANN.

Common options are as follows:

| Option | Description |
| --- | --- |
| `--full` | Install or update all asc-comm header files and Hcomm symbolic links in the package in full mode. |
| `--uninstall` | Restore the original files in the baseline and delete files that did not exist before installation. |
| `--check` | Verify the run package archive, payload and dependency compatibility without modifying CANN. |
| `--install-path=<PATH>` | Specify the Ascend installation root directory; the default is `/usr/local/Ascend` for root and `$HOME/Ascend` for non-root. |
| `--install-for-all` | Allow all users to read and access files in the package during installation; enabled by default for root installation. |
| `--force` | Discard user modifications recorded in the package management files during installation or uninstallation. |
| `--quiet` | Run silently, suitable for script calls. |
| `--list` | List files in the run package, provided by the makeself outer layer of the run package. |
| `-h`, `--help` | Display help information. |

`--full`, `--uninstall` and `--check` are mutually exclusive operations; only one can be specified at a time.

### Checking Package Contents

Before installation, you can check the archive and payload checksums, and view the package version, source commit, architecture, required asc-devkit, hcomm and runtime versions, and the number of files. This operation does not modify CANN:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --check
```

List the files in the run package:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --list
```

### Installation

Install to the default Ascend root directory:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --full
```

You can also explicitly specify the installation location using an absolute path:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --install-path=/path/to/cann
```

The installer tries in turn to resolve `--install-path` itself, `<install-path>/cann` or `<install-path>/ascend-toolkit/latest` as the CANN root directory. The resolved directory must contain `asc/include/adv_api/`, `asc/impl/adv_api/detail/` and `share/info/`, and the current user must be the owner of that CANN root directory.

When a non-root user needs to allow other users to use the installed content, run:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --install-for-all --install-path=/path/to/cann
```

- Permission behavior is consistent with the asc-devkit sub-package. For root installation, header file permissions are `555`, Hcomm directory permissions are `755`, and managed directory permissions under `comm_api` are `555`.
- Non-root installation sets the preceding permissions to `550`, `750` and `550` by default; with `--install-for-all`, they are set to `555`, `755` and `555`. The target CANN root directory and its parent directories need to allow other users to read and enter; otherwise the installer refuses to operate.
- The installer saves the managed files and correct Hcomm symbolic links that existed before installation. Symbolic links with wrong targets, or ordinary files or directories occupying symbolic link paths, are not overwritten.
- During packaging, the major and minor versions of asc-devkit, hcomm and runtime, as well as the system architecture, are recorded. The system architecture must match; when dependency versions are incompatible, a warning is printed and installation continues.

### Repeated Installation

Install the new run package to the same CANN directory with `--full` to update it. The installer keeps the files from before the first installation for subsequent uninstallation.

If the files or symbolic links managed by the package are modified manually, repeated installation refuses to overwrite them. After confirming that these modifications can be discarded, use:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --full --force --install-path=/path/to/cann
```

`--force` cannot skip the system architecture check or the in-package payload verification, and does not overwrite symbolic links with wrong targets or ordinary files or directories occupying symbolic link paths.

### Uninstallation

Restores the files from before installation. When `--install-path` was used for installation, the same path should be passed in during uninstallation:

```bash
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --uninstall
# Uninstallation method corresponding to the custom installation path
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run \
    --uninstall --install-path=/path/to/cann
```

Uninstallation restores the files and permissions that existed before installation, and deletes files and symbolic links newly added by the run package. If the files or symbolic links managed by the package are modified manually, uninstallation refuses to operate; after confirming that these modifications can be discarded, add `--force` to the uninstall command.

The installation, repeated installation and uninstallation processes write logs. The log directory is `/var/log/ascend_seclog` for the root user and `$HOME/var/log/ascend_seclog` for non-root users; process logs are written to `ascend_install.log`, and operation audits are written to `operation.log`.

After installation, the content to be verified needs to be rebuilt; existing `.o` or `.bin` files are not automatically updated.

The run package version is read from `version.cmake` in the repository root directory. You can directly call the packaging script to specify the output directory and the CANN used for packaging:

```bash
bash scripts/package/build_package.sh \
    --cann_path=/path/to/cann \
    --output-dir=/path/to/output
```

## CMake Entry

The CMake entry of UTs is:

```text
tests/ut/CMakeLists.txt
```

Common CMake variables:

| Variable | Description |
| --- | --- |
| `ASCEND_CANN_PACKAGE_PATH` | CANN package path. If not explicitly specified, it is deduced from environment variables first. |
| `PRODUCT_TYPE_LIST` | List of product types to build. By default, `ascend950pr_9599_AIV` and `ascend910B1_AIC` are included. |
| `TEST_MOD` | UT target filter. The default is `all`. |
| `ASCCOMM_UT_RUN_AFTER_BUILD` | Whether to run UTs after building. The default is `ON`. Set it to `OFF` if only building without running. |

## GTest Dependency

UTs search for the system GTest first. If there is no GTest on the system, you can point `CANN_3RD_LIB_PATH` to the CANN third_party directory.

```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<path-to-third-party>
```

You can also pass in the CANN third_party directory through the build script:

```bash
bash build.sh -t --cann_3rd_lib_path=<path-to-third-party>
```

## Sample Build and Run

`examples/hcomm_write_read_nbi` provides the AIV direct-drive URMA `WriteNbi` and `ReadNbi` point-to-point communication sample. The sample is built as an independent CMake project:

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/hcomm_write_read_nbi
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

By default, the sample can directly start two ranks:

```bash
./demo
```

You can also manually specify the ranks to run:

```bash
# Terminal 1: rank 0
./demo 0 2 tcp://127.0.0.1:29621

# Terminal 2: rank 1
./demo 1 2 tcp://127.0.0.1:29621
```

The sample supports Ascend 950PR/Ascend 950DT and requires CANN 9.1.0 or later. Running the sample requires at least two NPUs; a single-card environment only supports build verification.
