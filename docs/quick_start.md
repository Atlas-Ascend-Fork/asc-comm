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
      <td align="center" colspan="2"><a href="#cann-install">手动下载安装CANN包</a>（仅编译验证）</td>
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
> - 针对仅体验“编译本开源仓 + 编译验证样例”的用户，不要求主机带NPU设备，可跳过安装NPU驱动和固件，直接安装CANN包，请参考[下载安装CANN包](#cann-install)。样例运行至少需要2张NPU。

### CANN官方Docker镜像<a name="cann-docker-image"></a>

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
    | `--device /dev/davinci<N>` | 将指定的NPU设备映射到容器内；如需使用多张设备，可多次使用`--device`参数 | 根据`npu-smi info`显示的设备号调整。运行样例时至少映射两张Ascend 950PR&950DT系列产品的NPU设备。 |
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

本仓编译与样例验证依赖CANN toolkit包。

#### 下载CANN包

1. <a name="下载-cann-商用社区版"></a>下载CANN商用/社区版

    如果您想体验**官网正式发布的CANN包**，请访问[CANN安装部署-昇腾社区](https://www.hiascend.com/cann/download)获取对应版本CANN包。

2. <a name="下载-cann-master"></a>下载CANN master

    如果您想体验**CANN master**，请访问[CANN master obs镜像网站](https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master)，下载**日期最新**的CANN包。

#### 安装CANN包

1. 安装CANN toolkit包

    ```bash
    chmod +x Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run
    ./Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run --install --install-path=${install_path}
    ```

| 参数 | 说明 |
| :--- | :-------------------------------- |
| `${cann_version}` | CANN包版本号。 |
| `${install_path}` | 安装路径。root用户默认路径：`/usr/local/Ascend`，非root用户默认路径：`$HOME/Ascend`。|

## ✅ 环境验证<a name="cann-verify"></a>

> [!NOTE]
> 使用前须知
>
> CANN官方Docker镜像已预装CANN包，可直接执行以下命令验证。

验证环境和驱动是否正常：

- **检查NPU设备**：

    ```bash
    # 运行npu-smi，若能正常显示设备信息，则驱动正常
    npu-smi info
    ```

- **检查CANN包安装**：

    ```bash
    # 查看CANN toolkit的version字段提供的版本信息（默认路径安装）
    cat /usr/local/Ascend/cann/$(uname -m)-linux/ascend_toolkit_install.info
    ```

## ⚙️ 环境变量配置<a name="cann-env-setup"></a>

> [!NOTE]
> 使用前须知
>
> CANN官方Docker镜像已自动配置环境变量，可跳过此步骤。

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
>
> 如您使用**容器化技术**，容器中已为您安装好依赖，可跳过此步骤。

以下为本开源仓源码编译和UT验证的基础依赖条件：

- python >= 3.9.0
- gcc/g++支持C++17
- cmake >= 3.16.0

### ⚡ 编译安装<a name="compile-install"></a>

1. 编译

   本开源仓提供一键式编译能力。

   方式一：进入本开源仓代码根目录，执行如下命令：

   ```bash
   bash build.sh --pkg
   ```

   方式二：用户也可使用离线下载功能，手动下载[cann-cmake源码包](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/cmake/cmake-master-025.tar.gz)存放至自定义目录PATH_TO_DOWNLOAD，进入本开源仓代码根目录，执行如下命令：

   ```bash
   bash build.sh --pkg --cann_3rd_lib_path={PATH_TO_DOWNLOAD}  #PATH_TO_DOWNLOAD为自定义下载目录
   ```

   编译完成后，会在`build_out`目录下生成`cann-asc-comm_${asccomm_version}_linux-$(uname -m).run`软件包。其中`${asccomm_version}`为asc-comm版本号，取自仓库根目录的`version.cmake`。

   > [!NOTE]说明
   > 直接执行`bash build.sh`（不带`--pkg`）仅验证run包构建是否通过，不生成软件包，也不安装到CANN目录。

2. 安装

   在开源仓根目录下执行下列命令，将编译生成的run包安装到CANN包安装路径`${install_path}`（默认：root用户`/usr/local/Ascend`，非root用户`$HOME/Ascend`）：

   ```bash
   cd build_out
   # 默认路径安装run包
   ./cann-asc-comm_${asccomm_version}_linux-$(uname -m).run --full
   # 指定路径安装run包
   # ./cann-asc-comm_${asccomm_version}_linux-$(uname -m).run --full --install-path=${install_path}
   ```

   > [!NOTE]说明
   > 该run包为本地开发验证包，安装时会把包内的头文件、库与软链更新到目标CANN安装目录中的同名受管文件，覆盖前会备份原文件，可通过同路径执行`--uninstall`恢复；执行安装的用户需对CANN根目录具备属主权限，共享或由root持有的CANN请谨慎操作。详见[编译与测试](zh/compilation/build_and_test.md)。

### 🧪 UT验证<a name="ut-verify"></a>

#### 安装依赖

UT依赖googletest、mockcpp等第三方软件：googletest优先使用系统已安装版本，未安装时从仓库根目录的`third_party`目录获取；mockcpp、boost从`third_party`目录获取。也可通过`--cann_3rd_lib_path`指定三方库目录。

#### UT执行

方式一：在开源仓根目录执行下列命令，构建并执行asc-comm UT：

```bash
bash build.sh -t
```

方式二：用户也可使用离线下载功能，手动下载[三方库源码包](#开源第三方软件依赖)及cann-cmake源码包存放至自定义目录PATH_TO_DOWNLOAD，在开源仓根目录执行下列命令：

```bash
bash build.sh -t --cann_3rd_lib_path={PATH_TO_DOWNLOAD}  #PATH_TO_DOWNLOAD为自定义下载目录
```

#### 开源第三方软件依赖

在执行UT时，依赖的第三方开源软件列表如下：

| 开源软件 | 版本 | 下载地址 |
| :---: | :---: | :---: |
| googletest | 1.14.0 | [googletest-1.14.0.tar.gz](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/googletest/googletest-1.14.0.tar.gz) |
| boost | 1.87.0 | [boost_1_87_0.tar.gz](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/boost/boost_1_87_0.tar.gz) |
| mockcpp | 2.7 | [mockcpp-2.7.tar.gz](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/mockcpp/mockcpp-2.7.tar.gz) |
| mockcpp_patch | 2.7 | [mockcpp-2.7-h5.patch](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/mockcpp/mockcpp-2.7-h5.patch) |

### 🧩 样例验证<a name="sample-verify"></a>

本仓在[examples](../examples)目录下提供aicore、aicpu、ccu三类样例。各样例支持的CANN版本、设备数量与运行方式，以其目录下的README为准。

> [!IMPORTANT]请先通过官方兼容性查询确认OS/发行版后再编译验证
> 编译样例依赖CANN自带的Ascend C编译工具链。受支持的OS/发行版请以所用CANN版本的操作系统兼容性说明为准：
> 请访问[昇腾兼容性查询助手](https://www.hiascend.com/hardware/compatibility)，按实际的产品型号（Ascend 950PR&950DT
> 系列产品）、CPU架构、CANN版本和部署形态（物理机/虚拟机/容器）查询配套的OS及内核版本，
> 请勿仅凭通用发行版名称选型。在Debian 13（glibc 2.41）主机上，使用CANN 9.1.X自带工具链，曾观察到编译在系统
> 头文件`bits/floatn.h`处报`unsupported machine mode '__TC__'` / `__float128 is not supported on this target`
> （make Error 2）。该现象仅说明此工具链与该系统的组合未经支持，报错与样例代码无关，无法通过源码修改或
> `-U__HAVE_FLOAT128`等宏绕开，请优先使用[CANN官方Docker镜像](#cann-docker-image)完成编译验证。
