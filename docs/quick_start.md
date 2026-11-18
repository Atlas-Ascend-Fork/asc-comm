# 快速开始

## 🛠️ 环境准备<a name="prepare-install"></a>

根据**本地是否有NPU设备**和**使用目标**选择对应的环境准备方式：

<table>
  <thead>
    <tr>
      <th align="center">环境准备</th>
      <th align="center">社区体验/算子开发(CANN商用/社区版)</th>
      <th align="center">生态开发者贡献(CANN master)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center"><strong>无NPU设备</strong></td>
      <td align="center" colspan="2"><a href="#cloud-dev-env">云开发环境</a>+<a href="#cann-install">手动下载安装CANN包</a></td>
    </tr>
    <tr>
      <td align="center"><strong>有NPU设备</strong></td>
      <td align="center"><a href="#cann-docker-image">CANN官方Docker镜像</a></td>
      <td align="center"><a href="#cann-install">手动下载安装CANN包</a></td>
    </tr>
  </tbody>
</table>

> [!TIP]选择建议
>
> - 为了保障开发体验环境的质量，推荐用户基于**容器化技术**完成**环境准备**。
> - 如不希望使用容器，也可在带NPU设备的主机上完成**环境准备**，请参考[CANN软件安装指南 - 在物理机上安装](https://www.hiascend.com/cann/download)。
> - 针对仅体验“编译本开源仓 + 编译验证样例”的用户，不要求主机带NPU设备，可跳过安装NPU驱动和固件，直接安装CANN包，请参考[下载安装CANN包](#cann-install)。`hcomm_urma_write_read_nbi`样例运行需要至少2张NPU。

### 1️⃣ 云开发环境<a name="cloud-dev-env"></a>

对于无NPU设备的用户，可直接使用**CANNLab云开发环境**，即“**一站式开发平台**”，该平台提供了在线可直接运行的昇腾环境，环境中已安装必备的驱动固件、软件包和依赖，无需手动安装。

> [!NOTE]说明
>
> - 环境默认安装最新商发版CANN包，源码下载时注意与软件配套。更多关于一站式开发平台的介绍请参考[CANNLab指导](https://gitcode.com/org/cann/cannlab/docs)。
> - 如需下载特定版本的CANN包，请参考[下载安装CANN包](#cann-install)。

1. 进入开源项目，单击“`CANNLab`”按钮，若未注册或认证，请根据页面提示进行注册和认证。
2. 根据页面提示创建环境并配置规格，启动云开发环境后，单击“连接”进入一站式开发平台。

### 2️⃣ CANN官方Docker镜像<a name="cann-docker-image"></a>

对于有NPU设备的用户，可使用CANN官方Docker镜像进行开发体验。

1. 确认主机环境

   - 是否已安装NPU驱动和固件，使用`npu-smi info`能够输出NPU相关信息，如没有安装，请参考《[CANN软件安装指南](https://www.hiascend.com/document/redirect/CannCommunityInstWizard)》中“准备软件包”和“安装NPU驱动和固件”章节。驱动与固件是运行态依赖，若仅编译本项目源码，可以不安装。
   - 是否已安装Docker，使用`docker --version`能够输出Docker版本信息，如没有安装，请参考[Docker官方安装指南](https://docs.docker.com/engine/install/)。

2. 下载CANN镜像

    从[昇腾镜像仓库](https://www.hiascend.com/developer/ascendhub/detail/17da20d1c2b6493cb38765adeba85884)拉取已预集成CANN镜像：

    ```bash
    # 示例：ascend/cann:tag为9.0.0-beta.2的CANN社区包
    # docker pull swr.cn-south-1.myhuaweicloud.com/ascendhub/cann:9.0.0-beta.2-910b-ubuntu22.04-py3.11
    docker pull <ascend/cann:tag>
    ```

    > [!NOTE]
    > 使用说明
    > - 镜像默认安装了对应版本的CANN包，源码下载时注意与软件配套。
    > - 镜像文件比较大，正常网速下，下载时间约为5～10分钟，请您耐心等待。

3. 运行Docker

    拉取镜像后，需要以特定参数启动，以便容器内能访问宿主机的NPU设备。

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

    | 参数 | 说明 | 注意事项 |
    | :--- | :--- | :--- |
    | `--name <cann_container>` | 为容器指定名称，便于管理 | 自定义 |
    | `--ipc=host` | 与宿主机共享IPC命名空间，NPU进程间通信（共享内存、信号量）所需 | - |
    | `--net=host` | 使用宿主机网络栈，避免容器网络转发带来的通信延迟 | - |
    | `--privileged` | 赋予容器完整设备访问权限，NPU驱动正常工作所需 | - |
    | `--device /dev/davinci<N>` | 将指定的NPU设备映射到容器内；如需使用多张设备，可多次使用`--device`参数 | 根据`npu-smi info`显示的设备号调整。运行`hcomm_urma_write_read_nbi`样例时至少映射两张Ascend 950PR&950DT系列产品的NPU设备。 |
    | `--device /dev/davinci_manager` | 映射NPU设备管理接口 | - |
    | `--device /dev/devmm_svm` | 映射设备内存管理接口 | - |
    | `--device /dev/hisi_hdc` | 映射主机与设备间的通信接口 | - |
    | `-v /usr/local/dcmi:/usr/local/dcmi` | 挂载设备容器管理接口（DCMI）相关工具和库 | - |
    | `-v /usr/local/bin/npu-smi:/usr/local/bin/npu-smi` | 挂载`npu-smi`工具 | 使容器内可以直接运行此命令来查询NPU状态和性能信息 |
    | `-v /usr/local/Ascend/driver/lib64/:/usr/local/Ascend/driver/lib64/` | 将宿主机的NPU驱动库映射到容器内 | - |
    | `-v /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info` | 挂载驱动版本信息文件 | - |
    | `-v /etc/ascend_install.info:/etc/ascend_install.info` | 挂载CANN软件安装信息文件 | - |
    | `-v </home/your_host_dir>:</home/your_container_dir>` | 挂载宿主机的一个路径到容器中 | 自定义 |
    | `-it` | `-i`（交互式）和`-t`（分配伪终端）的组合参数 | - |
    | `<ascend/cann:tag>` | 指定要运行的Docker镜像 | 请确保此镜像名和标签（tag）与您通过`docker pull`拉取的镜像完全一致 |
    | `bash` | 容器启动后立即执行的命令 | - |

### 📥 下载安装CANN包<a name="cann-install"></a>

CANN包分为CANN toolkit包和CANN ops包。

#### 下载CANN包

1. <a name="下载-cann-商用社区版"></a>下载CANN商用/社区版

    如果您想体验**官网正式发布的CANN包**，请访问[CANN安装部署-昇腾社区](https://www.hiascend.com/cann/download)获取对应版本CANN包。

2. <a name="下载-cann-master"></a>下载CANN master

    如果您想体验**CANN master**，请访问[CANN master obs镜像网站](https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master)，下载**日期最新**的CANN包。

#### 安装CANN包

1. 安装CANN toolkit包 (必选)

    ```bash
    chmod +x Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run
    ./Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run --install --install-path=${install_path}
    ```

2. 安装CANN ops包 (可选)

    ```bash
    chmod +x Ascend-cann-${soc_name}-ops_${cann_version}_linux-$(uname -m).run
    ./Ascend-cann-${soc_name}-ops_${cann_version}_linux-$(uname -m).run --install --install-path=${install_path}
    ```

    > [!IMPORTANT]
    > 安装说明
    > 当前[Hcomm AIV直驱URMA样例](../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md)不依赖ops包；仅在后续使用依赖算子包的功能时按需安装。

| 参数 | 说明 |
| :--- | :-------------------------------- |
| `${cann_version}` | CANN包版本号。 |
| `${soc_name}` | NPU型号，如`910b`。 |
| `${install_path}` | 安装路径，toolkit包和ops包需相同。root用户默认路径：`/usr/local/Ascend`，非root用户默认路径：`$HOME/Ascend`。|

## ✅ 环境验证<a name="cann-verify"></a>

> [!NOTE]
> 使用前须知
> 云开发环境和CANN官方Docker镜像已预装CANN包，可直接执行以下命令验证。

验证环境和驱动是否正常：

- **检查NPU设备**：

    ```bash
    # 运行npu-smi，若能正常显示设备信息，则驱动正常
    npu-smi info
    ```

- **检查CANN包安装**：

    ```bash
    # 查看CANN Toolkit的version字段提供的版本信息（默认路径安装）
    cat /usr/local/Ascend/cann/$(uname -m)-linux/ascend_toolkit_install.info
    ```

## ⚙️ 环境变量配置<a name="cann-env-setup"></a>

> [!NOTE]
> 使用前须知
> 云开发环境和CANN官方Docker镜像已自动配置环境变量，可跳过此步骤。

按需选择合适的命令使环境变量生效：

```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# 指定路径安装
# source ${install_path}/cann/set_env.sh
```

## 🔨 源码编译步骤<a name="source-build"></a>

### 📥 下载源码<a name="source-download"></a>

开发者可通过如下命令下载本仓源码：

```bash
git clone https://gitcode.com/cann/asc-comm.git
cd asc-comm
```

### 📦 依赖检查<a name="dependency-check"></a>

> [!NOTE]
> 使用前须知
> 如您使用**容器化技术**，容器中已为您安装好依赖，可跳过此步骤。

以下为本开源仓源码编译和UT验证的基础依赖条件：

- python >= 3.7.0
- gcc/g++支持C++17
- cmake >= 3.16.0

### ⚡ 编译安装<a name="compile-install"></a>

进入本开源仓代码根目录，执行如下命令：

```bash
bash build.sh
```

### 🧪 UT验证<a name="ut-verify"></a>

#### 安装依赖

UT依赖googletest。若系统中没有GTest，可以通过`CANN_3RD_LIB_PATH`指向CANN third_party目录。

#### UT执行

方式一：在开源仓根目录执行下列命令，构建Hcomm UT。

```bash
bash build.sh -t
```

需要指定CANN third_party目录时执行：

```bash
bash build.sh -t --cann_3rd_lib_path=<path-to-third-party>
```

方式二：用户也可直接使用CMake命令指定离线GTest路径。

```bash
# 1. 先配置CANN环境变量（测试目标运行时会加载 libruntime_common.so 等运行库，跳过会导致RC=127运行失败）；将${install_path}替换为CANN安装路径（默认/usr/local/Ascend，非root用户为$HOME/Ascend）
source ${install_path}/cann/set_env.sh
# 2. 使用独立构建目录，请勿复用方式一 build.sh -t 生成的 build/ut-hcomm 缓存
cmake -S tests/ut -B build/ut-hcomm-cmake -DCANN_3RD_LIB_PATH=<path-to-third-party>
cmake --build build/ut-hcomm-cmake
# 3. CCU和AICPU用例分别注册在子目录，需分别进入执行CTest（与 build.sh -t 行为一致）
(cd build/ut-hcomm-cmake/ccu_hcomm && ctest --output-on-failure)
(cd build/ut-hcomm-cmake/aicpu_asccomm && ctest --output-on-failure)
```

> [!NOTE]注意事项
>
> - CCU和AICPU的用例分别注册在构建目录的`ccu_hcomm`和`aicpu_asccomm`子目录（与`build.sh -t`的行为一致），需分别进入对应目录执行`ctest`。在构建根目录直接执行`ctest`会提示`No tests were found!!!`且退出码为0，易误判为UT通过，可先用`ctest -N`确认用例注册情况。
> - 方式一的`build.sh -t`默认在`build/ut-hcomm`目录构建。若对该目录已生成的CMake缓存再次直接执行`cmake`，会因mockcpp等第三方缓存源目录与缓存记录不一致而报错（`The source ".../mockcpp/CMakeLists.txt" does not match the source "... used to generate cache"`）并退出。方式二请改用独立目录（如`build/ut-hcomm-cmake`），或先清理旧缓存。
> - 直接执行`cmake`时，`build.sh`中的`set_env()`环境检查不再生效。请先`source set_env.sh`，否则在独立shell执行`ctest`时，依赖runtime运行库的用例会因`error while loading shared libraries: libruntime_common.so`（退出码127）静默失败，`LastTest.log`仅显示`Test Failed.`无任何原因。

#### 开源第三方软件依赖

在执行UT时，依赖的第三方开源软件列表如下：

| 开源软件 | 版本 |
| :---: | :---: |
| googletest | 1.14.0 |

### 🧩 样例验证<a name="sample-verify"></a>

[hcomm_urma_write_read_nbi](../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md)提供AIV直驱URMA `WriteNbi`和`ReadNbi`点对点通信样例。样例支持Ascend 950PR&950DT系列产品，要求CANN 9.1.0或以上版本。运行样例需要至少2张NPU；单卡环境仅支持编译验证。

进入样例目录后执行：

> [!IMPORTANT]请先通过官方兼容性查询确认OS/发行版后再编译验证
> 编译样例依赖CANN自带的Ascend C编译工具链。受支持的OS/发行版请以所用CANN版本的操作系统兼容性说明为准：
> 请访问[昇腾兼容性查询助手](https://www.hiascend.com/hardware/compatibility)，按实际的产品型号（Ascend 950PR&950DT
> 系列产品）、CPU架构、CANN版本（>= 9.1.0）和部署形态（物理机/虚拟机/容器）查询配套的OS及内核版本，
> 请勿仅凭通用发行版名称选型。在Debian 13（glibc 2.41）主机上，使用CANN 9.1.X自带工具链执行下列命令，
> 曾观察到编译在系统头文件`bits/floatn.h`处报`unsupported machine mode '__TC__'` /
> `__float128 is not supported on this target`（make Error 2）。该现象仅说明此工具链与该系统的组合未经
> 支持，报错与样例代码无关，无法通过源码修改或`-U__HAVE_FLOAT128`等宏绕开，
> 请优先使用[CANN官方Docker镜像](#cann-docker-image)或CANNLab云开发环境完成编译验证。

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/01_hcomm_urma_write_read_nbi
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./hcomm_write_read_nbi tcp://127.0.0.1:29623 <nranks>
```

执行成功时输出如下：

```text
[rank 0] hcomm_write_read_nbi | sent to rank 1, received from rank 1 | PASS
[rank 1] hcomm_write_read_nbi | sent to rank 0, received from rank 0 | PASS
RESULT | Example=hcomm_write_read_nbi | Status=PASS
```
