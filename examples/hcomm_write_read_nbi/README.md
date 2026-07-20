# Hcomm WriteNbi/ReadNbi 点对点通信样例 (AIV直驱URMA)

## 概述

本样例展示如何在 Ascend C Kernel 中，基于 **AIV直驱URMA** 架构，使用 `Hcomm` 类的 **`WriteNbi`** 和 **`ReadNbi`** API 实现 NPU 间极低延迟的点对点（P2P）通信。样例中两张卡对称执行 `WriteNbi` + `ReadNbi`，通过地址偏移区分数据段，相互写入并读出数据，最后在 Host 侧校验结果一致性。

## 支持的产品及 CANN 软件版本

| 产品 | CANN 软件版本 |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
├── hcomm_write_read_nbi
│   ├── CMakeLists.txt              // 编译工程文件
│   ├── README.md                   // 样例说明文档
│   ├── hcomm_write_read_nbi.asc    // Ascend C 样例实现（Kernel + Host）
│   ├── utils.cpp                   // 工具函数实现
│   └── utils.h                     // 工具函数声明
```

## 样例描述

### 样例功能
本样例重点演示 **AIV 直驱 URMA** 场景下的 P2P 通信接口。与传统的集合通信（如 `AlltoAll`）不同，他们都是由 Host 发起通信，**但 AIV 直驱 URMA 的每次数据面的通信不再需要 Host 参与**。它们允许 NPU 硬件直接向目标 NPU 发起 URMA 操作，绕过传统软件协议栈，实现显存到显存的直接访问，具有极低的通信延迟。此特性非常适用于 MoE (Dispatch/Combine)、Pipeline 并行等对延迟敏感的分布式训练场景。

| API | 数据流向 | 语义说明 (AIV直驱URMA) |
|------|---------|------|
| `WriteNbi` | 本地 GM → 远端 GM | AIV 直驱 URMA 写接口：将本地 Global Memory 数据直接写入远端 NPU 指定地址，无需远端 CPU 介入。 |
| `ReadNbi` | 远端 GM → 本地 GM | AIV 直驱 URMA 读接口：直接从远端 NPU 指定地址读取数据到本地 Global Memory，无需远端 CPU 介入。 |

### 样例实现

#### 1. Host 侧通信域准备
在 Ascend 950 系列上，通信域需以多进程方式创建（每个进程对应一个 rank）。关键步骤如下，重点体现 AIV 直驱模式的配置：

1. **交换 RootInfo**：Rank 0 调用 `HcclGetRootInfo` 获取 root 信息，通过 TCP 发送给 Rank 1。
2. **创建通信域**：各 Rank 调用 `HcclCommInitRootInfoConfig` 创建通信域。**注意：AIV 直驱模式下，无需配置 `hcclOpExpansionMode`。**
3. **注册通信内存**：调用 `HcclCommMemReg` 向通信域注册本卡的通信 buffer，Channel 创建时该内存信息会自动交换给对端。
4. **获取链路 Endpoint**：通过 `HcclRankGraphGetLayers` 和 `HcclRankGraphGetLinks` 获取本 Rank 到对端的物理链路 Endpoint 信息。
5. **创建 P2P 通道 (AIV直驱)**：调用 `HcclChannelAcquire` 创建到对端的 P2P 通道。此处需明确指定引擎为 `COMM_ENGINE_AIV`，协议为 `COMM_PROTOCOL_UBC_CTP`（即 URMA 协议），并传入通知数量和待交换的内存句柄。
6. **获取对端内存地址**：调用 `HcclChannelGetRemoteMems` 获取对端注册的内存地址，作为 Kernel 侧 `WriteNbi`/`ReadNbi` 的远端目标地址。
7. **下发 Context**：Host 预初始化 seg0（填充基于 rankId 的伪随机 pattern），将 `ChannelHandle` 和 buffer 地址封装到 `CommContext` 并下发到各卡 GM。

```cpp
// 1. 各 rank 各自创建通信域 (AIV直驱模式无需配置 hcclOpExpansionMode)
HcclCommConfig config;
HcclCommConfigInit(&config);
config.hcclWorldRankID = rank;
HcclCommInitRootInfoConfig(nranks, &rootInfo, rank, &config, &comm);

// 2. 注册通信内存
HcclCommMemReg(comm, "shareBuf", &regMem, &memHandle);

// 3. 获取链路 endpoint
HcclRankGraphGetLinks(comm, layerId, rank, peerRank, &links, &linkNum);

// 4. 创建 AIV 直驱 URMA P2P 通道
channelDesc.remoteRank = peerRank;
channelDesc.channelProtocol = COMM_PROTOCOL_UBC_CTP; // URMA 协议
channelDesc.memHandles = &memHandle;
channelDesc.memHandleNum = 1;
HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &channel); // 指定 COMM_ENGINE_AIV

// 5. 获取对端内存地址
HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags);
```

#### 2. Kernel 侧执行
通信流程分为三步：`Init()` → `WriteNbi()`/`ReadNbi()` → `Drain()`。两卡执行相同的 Kernel 逻辑，通过地址偏移区分三段数据：
- **seg0** `[0, DATA_SIZE)`：本卡 pattern（Host 预初始化）
- **seg1** `[DATA_SIZE, 2*DATA_SIZE)`：接收对端 `WriteNbi` 写入的数据
- **seg2** `[2*DATA_SIZE, 3*DATA_SIZE)`：接收本卡 `ReadNbi` 从对端 seg0 读回的数据

- **`Init`**：分配 UB 工作空间（>= 512 字节），用于存放 Hcomm 内部 WQE/CQE 等状态。
- **`WriteNbi` / `ReadNbi`**：调用 **AIV直驱URMA API** 将通信任务入队到 SQ。默认 `commit=true`，入队后立即触发 doorbell；如需批量优化，可改用 `commit=false` 多次入队后统一调用 `Commit()`。
- **`Drain`**：轮询 CQ 等待通信任务完成，返回后数据可见性才有保证。

```cpp
// 两卡对称执行 AIV 直驱 URMA 通信：
// 1. 本卡 seg0 → 对端 seg1 (WriteNbi)
hcomm_.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);

// 2. 对端 seg0 → 本卡 seg2 (ReadNbi)
hcomm_.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);

// 3. 等待 AIV 硬件通信任务完成
hcomm_.Drain(channel);
```

#### 3. 调用实现
多进程对称执行：两卡同时 launch kernel，无需分阶段同步。Host 侧预初始化 seg0 后通过 `TcpBarrier` 确保两卡就绪，再各自调用 Kernel。使用内核调用符 `<<<>>>` 调用核函数。

### 校验机制
- Host 预初始化 seg0 时，通过两次线性同余伪随机数生成器（LCG，使用 Knuth 乘法哈希常数 `0x9E3779B9U` 等参数）生成用于通信校验的随机 pattern，确保数据来源可区分。
- Kernel 执行完毕后，Host 侧通过 `aclrtMemcpy` 回读 `CommContext::testResult`。
- Host 侧进一步校验 seg1（对端 `WriteNbi` 写入）和 seg2（本卡 `ReadNbi` 读回）的数据是否与对端 pattern 完全一致。当校验结果码 testResult 为 0（即 seg1/seg2 数据与对端 pattern 完全一致）时，打印 test pass!。

## 编译与运行

在本样例根目录下执行如下步骤，编译并执行样例。

### 1. 配置环境变量
请根据当前环境上 CANN 开发套件包的安装方式，配置环境变量：
```bash
source ${install_path}/cann/set_env.sh
```
> **说明：** `${install_path}` 为 CANN 包安装目录，未指定安装目录时默认安装至 `/usr/local/Ascend` 下。

### 2. 编译工程
在本样例目录下执行如下命令：
```bash
mkdir -p build && cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

### 3. 样例执行
本样例支持两种执行方式：

**方式一：自动 Fork 多进程（推荐）**
直接执行即可，主进程会自动 fork 两个子进程（两卡对称执行 WriteNbi + ReadNbi）：
```bash
./demo
```

**方式二：手动指定参数单进程运行**
可在两个不同的终端中分别手动启动 Rank 0 和 Rank 1：
```bash
# 终端 1：启动 rank 0（绑定卡 0）
./demo 0 2 tcp://127.0.0.1:29621

# 终端 2：启动 rank 1（绑定卡 1）
./demo 1 2 tcp://127.0.0.1:29621
```

### 4. 编译选项说明
| 选项 | 可选值 | 说明 |
|------|--------|------|
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU 架构：`dav-3510` 对应 Ascend 950PR / Ascend 950DT |

### 5. 执行结果
执行成功后，终端将输出如下信息，说明 AIV 直驱 URMA 通信成功（两卡对称写入读出、结果一致）：
```text
rank 0 test pass!
rank 1 test pass!
test pass!
```
> **注意：** 单卡环境仅支持编译验证，实际运行本样例需至少配备 2 张 NPU。