# Quick Start

## 🛠️ Environment Preparation<a name="prepare-install"></a>

Select the corresponding environment setup method according to **whether local NPU devices are available** and your usage goals:

<table>
  <thead>
    <tr>
      <th align="center">Environment Option</th>
      <th align="center">Community Evaluation / Operator Development (CANN Commercial / Community Release)</th>
      <th align="center">Ecosystem Developer Contribution (CANN master)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center"><strong>No NPU Device</strong></td>
      <td align="center" colspan="2"><a href="#cann-install">Manual CANN Package Download & Installation</a> (compile verification only)</td>
    </tr>
    <tr>
      <td align="center"><strong>NPU Device Available</strong></td>
      <td align="center"><a href="#cann-docker-image">Official CANN Docker Image</a></td>
      <td align="center"><a href="#cann-install">Manual CANN Package Download & Installation</a></td>
    </tr>
  </tbody>
</table>

> [!TIP] Recommendations
>
> - For stable development experience, it is recommended to prepare your environment using **containerization technology**.
> - If you prefer not to use containers, you may set up the environment on a physical machine equipped with NPUs. Refer to [CANN Software Installation Guide - Physical Machine Installation](https://www.hiascend.com/cann/download).
> - Users who only intend to compile this open-source repository and perform compilation verification of samples do not require local NPUs. You can skip NPU driver and firmware deployment and directly install the CANN package; see [Download and Install CANN Package](#cann-install). Samples require at least two NPUs for runtime execution.

### Official CANN Docker Image<a name="cann-docker-image"></a>

Users with physical NPU hardware can develop with the official CANN Docker image.

1. Verify host prerequisites

   - Confirm NPU driver and firmware are installed. Run `npu-smi info` to check device status. If missing, follow the sections *Prepare Software Packages* and *Install NPU Driver and Firmware* in the [CANN Software Installation Guide](https://www.hiascend.com/document/redirect/CannCommunityInstWizard). Drivers and firmware are runtime dependencies; you may skip them if only compiling source code.
   - Confirm Docker is installed. Run `docker --version` to verify. If missing, follow the [official Docker installation guide](https://docs.docker.com/engine/install/).

2. Pull the CANN image

    Fetch the pre-integrated CANN image from the [Ascend Hub repository](https://www.hiascend.com/developer/ascendhub/detail/17da20d1c2b6493cb38765adeba85884):

    ```bash
    # Example: CANN community package tag 9.0.0-beta.2
    # docker pull swr.cn-south-1.myhuaweicloud.com/ascendhub/cann:9.0.0-beta.2-910b-ubuntu22.04-py3.11
    docker pull <ascend/cann:tag>
    ```

    > [!NOTE]
    > Usage Notes
    > - The image contains the corresponding CANN release. Match your source code with the software version.
    > - Image size is large; downloading normally takes 5–10 minutes under standard network conditions.

3. Launch the Docker container

    After pulling the image, start the container with dedicated parameters to grant access to host NPUs.

    ```bash
    docker run --name <cann_container> \
        --ipc=host --net=host --privileged \
        --device /dev/davinci0 \
        --device /dev/davinci1 \
        --device /dev/davinci_manager \
        --device /dev/devmm_svm \
        --device /dev/hisi_hdc \
        -v /usr/local/dcmi:/usr/local/dcmi \
        -v /usr/local/bin/npu-smi:/usr/local/bin/npu-smi \
        -v /usr/local/Ascend/driver/lib64/:/usr/local/Ascend/driver/lib64/ \
        -v /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info \
        -v /etc/ascend_install.info:/etc/ascend_install.info \
        -v </home/your_host_dir>:</home/your_container_dir> \
        -it <ascend/cann:tag> bash
    ```

    | Parameter | Description | Remarks |
    | :--- | :--- | :--- |
    | `--name <cann_container>` | Assign a custom name for container management | User-defined |
    | `--ipc=host` | Share IPC namespace with host; required for NPU inter-process communication (shared memory, semaphores) | - |
    | `--net=host` | Use host network stack to avoid communication latency from container forwarding | - |
    | `--privileged` | Grant full device access permissions required for NPU driver operation | - |
    | `--device /dev/davinci<N>` | Map specified NPU device into the container; repeat the argument to attach multiple NPUs | Adjust device index according to `npu-smi info`. At least two devices of the Ascend 950PR&950DT products shall be mapped to run the samples. |
    | `--device /dev/davinci_manager` | Mount NPU device management interface | - |
    | `--device /dev/devmm_svm` | Mount device memory management interface | - |
    | `--device /dev/hisi_hdc` | Mount host-device communication interface | - |
    | `-v /usr/local/dcmi:/usr/local/dcmi` | Mount DCMI tools and libraries for device management | - |
    | `-v /usr/local/bin/npu-smi:/usr/local/bin/npu-smi` | Mount `npu-smi` utility | Enables querying NPU status and performance inside the container |
    | `-v /usr/local/Ascend/driver/lib64/:/usr/local/Ascend/driver/lib64/` | Map host NPU driver libraries into container | - |
    | `-v /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info` | Mount driver version file | - |
    | `-v /etc/ascend_install.info:/etc/ascend_install.info` | Mount CANN installation metadata file | - |
    | `-v </home/your_host_dir>:</home/your_container_dir>` | Mount a host directory into the container | User-defined |
    | `-it` | Combined `-i` (interactive) and `-t` (pseudo-TTY) flags | - |
    | `<ascend/cann:tag>` | Target Docker image name and tag | Must exactly match the tag used in `docker pull` |
    | `bash` | Command executed immediately after container startup | - |

### 📥 Download and Install CANN Package<a name="cann-install"></a>

This repository requires the CANN toolkit package for compilation and sample verification.

#### Download CANN Packages

1. <a name="download-cann-commercial-community"></a>Download CANN Commercial / Community Release

    To use officially published CANN builds, visit [CANN Download Page - Ascend Community](https://www.hiascend.com/cann/download) to obtain the corresponding release.

2. <a name="download-cann-master"></a>Download CANN master

    To test CANN master branch builds, visit the [CANN master OBS mirror website](https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master) and download the most recent CANN packages by date.

#### Install CANN Packages

1. Install CANN toolkit package

    ```bash
    chmod +x Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run
    ./Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run --install --install-path=${install_path}
    ```

| Parameter | Description |
| :--- | :-------------------------------- |
| `${cann_version}` | CANN package version string. |
| `${install_path}` | Installation directory. Default: `/usr/local/Ascend` for root users, `$HOME/Ascend` for non-root users. |

## ✅ Environment Verification<a name="cann-verify"></a>

> [!NOTE]
> Precondition
>
> Official CANN Docker images come with pre-installed CANN packages; you may directly execute the verification commands.

Verify environment and driver health:

- **Check NPU devices**:

    ```bash
    # Normal output indicates functional drivers
    npu-smi info
    ```

- **Check CANN package installation**:

    ```bash
    # View CANN toolkit version info (default installation path)
    cat /usr/local/Ascend/cann/$(uname -m)-linux/ascend_toolkit_install.info
    ```

## ⚙️ Environment Variable Setup<a name="cann-env-setup"></a>

> [!NOTE]
> Precondition
>
> Official CANN Docker images configure environment variables automatically; skip this section.

Select the corresponding command to load environment variables:

```bash
# Default installation path (root user example; replace /usr/local with ${HOME} for non-root users)
source /usr/local/Ascend/cann/set_env.sh
# Custom installation path
# source ${install_path}/cann/set_env.sh
```

## 🔨 Source Compilation Steps<a name="source-build"></a>

### 📥 Clone Source Code<a name="source-download"></a>

Clone this repository:

```bash
git clone https://gitcode.com/cann/asc-comm.git
cd asc-comm
```

### 📦 Dependency Check<a name="dependency-check"></a>

> [!NOTE]
> Precondition
>
> If you use **containerization technology**, required dependencies are pre-installed inside the container and this step can be skipped.

Prerequisites for source compilation and UT validation:

- python >= 3.9.0
- gcc/g++ with C++17 support
- cmake >= 3.16.0

### ⚡ Build Source Code<a name="compile-install"></a>

1. Build

   This repository supports one-click compilation.

   Option 1: Enter the repository root and execute:

   ```bash
   bash build.sh --pkg
   ```

   Option 2: You can also use the offline download feature. Download the [cann-cmake source package](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/cmake/cmake-master-025.tar.gz) to a custom directory PATH_TO_DOWNLOAD, then run the following command in the repository root:

   ```bash
   bash build.sh --pkg --cann_3rd_lib_path={PATH_TO_DOWNLOAD}  #PATH_TO_DOWNLOAD is the custom download directory
   ```

   After the build completes, a `cann-asc-comm_${asccomm_version}_linux-$(uname -m).run` package is generated in the `build_out` directory. `${asccomm_version}` is the asc-comm version, read from `version.cmake` in the repository root.

   > [!NOTE]Note
   > Running `bash build.sh` without `--pkg` only verifies the run package build; it neither generates the package nor installs it into the CANN directory.

2. Install

   Run the following commands in the repository root to install the package to the CANN installation path `${install_path}` (default: `/usr/local/Ascend` for root users, `$HOME/Ascend` for non-root users):

   ```bash
   cd build_out
   # Install to the default path
   ./cann-asc-comm_${asccomm_version}_linux-$(uname -m).run --full
   # Install to a custom path
   # ./cann-asc-comm_${asccomm_version}_linux-$(uname -m).run --full --install-path=${install_path}
   ```

   > [!NOTE]Note
   > This run package is a local development verification package. Installation updates the managed headers, libraries, and links in the target CANN installation with the packaged versions, backs up the originals before overwriting, and can be reverted by running `--uninstall` on the same path. The installing user must own the CANN root directory; be careful when the CANN installation is shared or owned by root. See [Compilation and Testing](en/compilation/build_and_test.md).

### 🧪 Unit Test Verification<a name="ut-verify"></a>

#### Dependency Preparation

UTs depend on third-party software such as googletest and mockcpp: googletest uses the system-installed version first and falls back to the `third_party` directory in the repository root; mockcpp and boost are taken from `third_party`. Use `--cann_3rd_lib_path` to specify a custom third-party directory.

#### Run UTs

Option 1: Build and run asc-comm UTs from the repository root:

```bash
bash build.sh -t
```

Option 2: You can also use the offline download feature. Download the [third-party source packages](#open-source-third-party-dependencies) and the cann-cmake source package to a custom directory PATH_TO_DOWNLOAD, then run the following command in the repository root:

```bash
bash build.sh -t --cann_3rd_lib_path={PATH_TO_DOWNLOAD}  #PATH_TO_DOWNLOAD is the custom download directory
```

#### Open-Source Third-Party Dependencies

Third-party open-source software used for UT execution:

| Software | Version | Download |
| :---: | :---: | :---: |
| googletest | 1.14.0 | [googletest-1.14.0.tar.gz](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/googletest/googletest-1.14.0.tar.gz) |
| boost | 1.87.0 | [boost_1_87_0.tar.gz](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/boost/boost_1_87_0.tar.gz) |
| mockcpp | 2.7 | [mockcpp-2.7.tar.gz](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/mockcpp/mockcpp-2.7.tar.gz) |
| mockcpp_patch | 2.7 | [mockcpp-2.7-h5.patch](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/mockcpp/mockcpp-2.7-h5.patch) |

### 🧩 Sample Verification<a name="sample-verify"></a>

This repository provides aicore, aicpu, and ccu sample categories under the [examples](../examples) directory. The supported CANN version, device count, and runtime method of each sample are described in its own README.

> [!IMPORTANT]Confirm OS/distribution support via the official compatibility query before compiling
> Compiling a sample relies on the Ascend C compiler toolchain bundled with CANN. For supported
> OS/distributions, follow the OS compatibility statement of the CANN version in use: visit the
> [Ascend Compatibility Query Assistant](https://www.hiascend.com/hardware/compatibility) and query by your
> actual product model (Ascend 950PR&950DT products), CPU architecture, CANN version, and deployment form
> (physical machine, virtual machine, or container) for the matching OS and kernel versions; do not choose
> an OS based only on generic distribution names. On a Debian 13 (glibc 2.41) host, with the CANN 9.1.X
> bundled toolchain, compilation has been observed to fail in the system header `bits/floatn.h` with
> `unsupported machine mode '__TC__'` / `__float128 is not supported on this target` (make Error 2).
> This observation only indicates that this toolchain/system combination is unsupported; the error is
> unrelated to the sample code and cannot be bypassed by source changes or macros such as
> `-U__HAVE_FLOAT128`. Please use the [CANN Docker image](#cann-docker-image) for compilation verification.
