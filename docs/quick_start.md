# 快速开始

## 🛠️ 环境准备<a name="prepare&install"></a>

根据**本地是否有NPU设备**和**使用目标**选择对应的环境准备方式：

| 环境准备 | 社区体验/本仓构建验证 | 生态开发者贡献 |
| :---: | :---: | :---: |
| **无NPU设备** | [云开发环境](#cloud-dev-env)或[手动下载安装CANN包](#cann-install) | [手动下载安装CANN包](#cann-install) |
| **有NPU设备** | [CANN官方Docker镜像](#cann-docker-image)或物理机环境 | [CANN官方Docker镜像](#cann-docker-image)或物理机环境 |

> [!TIP]选择建议
>
> - 为了保障开发体验环境的质量，推荐用户基于**容器化技术**完成**环境准备**。
> - 如不希望使用容器，也可在带NPU设备的主机上完成**环境准备**，请参考[CANN软件安装指南 - 在物理机上安装](https://www.hiascend.com/cann/download)。
> - 针对仅体验"编译本开源仓 + UT验证"的用户，不要求主机带NPU设备，可跳过安装NPU驱动和固件，直接安装CANN包，请参考[下载安装CANN包](#cann-install)。

### 1️⃣ 云开发环境<a name="cloud-dev-env"></a>

对于无NPU设备的用户，可直接使用**CANNLab云开发环境**，即"**一站式开发平台**"。云开发环境中已安装必备的软件包和依赖，可完成代码获取、源码编译和UT验证。

1. 进入开源仓Gitcode页面，单击"`CANNLab >云开发`"按钮，使用已认证过的华为云账号登录。若未注册或认证，请根据页面提示进行注册和认证。
2. 根据页面提示创建NPU环境并配置规格，启动云开发环境后，单击"`连接 > WebIDE或Visual Studio Code`"进入一站式开发平台。开源项目的资源默认在`/mnt/workspace`目录下。

> [!NOTE]使用说明
>
> - 环境默认安装了配套的NPU驱动和固件、CANN包，源码下载时注意与软件配套。
> - 如需下载特定版本的CANN包，请参考[下载安装CANN包](#cann-install)。

### 2️⃣ CANN官方Docker镜像<a name="cann-docker-image"></a>

对于有NPU设备的用户，可使用CANN官方Docker镜像进行开发体验。

1. 确认主机环境。

   - 是否已安装NPU驱动和固件，使用`npu-smi info`能够输出NPU相关信息。
   - 是否已安装Docker，使用`docker --version`能够输出Docker版本信息。

2. 下载CANN镜像。

   ```bash
   docker pull <ascend/cann:tag>
   ```

3. 运行Docker。

   ```bash
   docker run --name <cann_container> \
       --ipc=host --net=host --privileged \
       -v </home/your_host_dir>:</home/your_container_dir> \
       -it <ascend/cann:tag> bash
   ```

### 📥 下载安装CANN包<a name="cann-install"></a>

CANN包分为CANN toolkit包和CANN ops包。本仓源码构建依赖CANN toolkit包。

#### 下载CANN包

如果您想体验**官网正式发布的CANN包**，请访问[CANN安装部署-昇腾社区](https://www.hiascend.com/cann/download)获取对应版本CANN包。

#### 安装CANN包

安装CANN toolkit包：

```bash
chmod +x Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run
./Ascend-cann-toolkit_${cann_version}_linux-$(uname -m).run --install --install-path=${install_path}
```

| 参数 | 说明 |
| :--- | :--- |
| `${cann_version}` | CANN包版本号 |
| `${install_path}` | 安装路径。默认：root用户`/usr/local/Ascend`，非root用户`$HOME/Ascend` |

## ✅ 环境验证<a name="cann-verify"></a>

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

以下为本开源仓源码编译和UT验证的基础依赖条件：

- python >= 3.7.0
- gcc/g++支持C++17
- cmake >= 3.16.0

### ⚡ 编译安装<a name="compile&install"></a>

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

方式二：用户也可直接使用CMake命令指定离线GTest路径。

```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<path-to-third-party>
cmake --build build/ut-hcomm
```

#### 开源第三方软件依赖

在执行UT时，依赖的第三方开源软件列表如下：

| 开源软件 | 版本 |
| :---: | :---: |
| googletest | 1.14.0 |
