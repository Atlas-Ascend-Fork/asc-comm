# Hcomm AIV直驱URMA WriteNbi/ReadNbi点对点通信样例

## 概述

本样例展示如何在Ascend C AIV Kernel中，基于**AIV直驱URMA**架构，使用`Hcomm`类的`WriteNbi`和`ReadNbi`接口实现NPU间低时延的点对点（P2P）通信。样例进程数由`./demo [nranks]`指定；不传入参数时默认启动双卡。每个rank在环形拓扑中与前后相邻rank建立P2P通道，通过地址偏移区分数据段，最后在Host侧校验通信结果。

## 支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
├── hcomm_write_read_nbi
│   ├── CMakeLists.txt              // 编译工程文件
│   ├── README.md                   // 样例说明文档
│   ├── README_en.md                // 英文样例说明文档
│   ├── hcomm_write_read_nbi.asc         // Host侧资源准备与Kernel调用
│   ├── hcomm_write_read_nbi_kernel.cpp  // AIV Kernel侧Hcomm调用
│   ├── hcomm_rw_def.h              // Host与Kernel共享定义
│   ├── utils.cpp                   // 工具函数实现
│   └── utils.h                     // 工具函数声明
```

## 样例描述

### 样例功能
本样例重点演示**AIV直驱URMA**场景下的P2P通信接口。Host侧只负责创建通信域、注册通信内存和建立通道；通信任务由AIV Kernel直接提交，数据面执行期间不需要Host逐次参与。该模式适用于MoE Dispatch/Combine、Pipeline并行等对通信时延敏感的场景。

| API | 数据流向 | 语义说明 (AIV直驱URMA) |
|------|---------|------|
| `WriteNbi` | 本地GM → 远端GM | AIV直驱URMA写接口：将本地Global Memory数据直接写入远端NPU指定地址，无需远端CPU介入。 |
| `ReadNbi` | 远端GM → 本地GM | AIV直驱URMA读接口：直接从远端NPU指定地址读取数据到本地Global Memory，无需远端CPU介入。 |

### 样例实现

#### 1. Host侧通信域准备
在Ascend 950系列上，通信域需以多进程方式创建（每个进程对应一个rank）。关键步骤如下，重点体现AIV直驱模式的配置：

1. **交换RootInfo**：Rank 0调用`HcclGetRootInfo`获取root信息，通过TCP发送给其他rank。
2. **创建通信域**：各Rank调用`HcclCommInitRootInfoConfig`创建通信域。**注意：AIV直驱模式下，无需配置`hcclOpExpansionMode`。**
3. **注册通信内存**：调用`HcclCommMemReg`向通信域注册本卡的通信buffer，Channel创建时该内存信息会自动交换给对端。
4. **建立TCP环形拓扑**：每个rank监听`BASE_PORT + rank`，主动连接`next = (rank + 1) % nranks`，同时接受来自`prev = (rank - 1 + nranks) % nranks`的连接。该环形连接用于RootInfo交换后的Host侧barrier，确保各rank在关键阶段同步推进。
5. **获取链路Endpoint**：通过`HcclRankGraphGetLayers`和`HcclRankGraphGetLinks`分别获取本Rank到`prev`和`next`的物理链路Endpoint信息。
6. **创建P2P通道（AIV直驱）**：调用`HcclChannelAcquire`创建到相邻rank的P2P通道。此处需明确指定引擎为`COMM_ENGINE_AIV`、协议为`COMM_PROTOCOL_UBC_CTP`（URMA协议），并传入待交换的内存句柄。
7. **获取对端内存地址**：调用`HcclChannelGetRemoteMems`获取相邻rank注册的内存地址，作为Kernel侧`WriteNbi`/`ReadNbi`的远端目标地址。
8. **下发Context**：Host预初始化seg0（填充基于rankId的伪随机pattern），将`ChannelHandle`和buffer地址封装到`CommContext`并下发到各卡GM。

```cpp
// 1. 各rank各自创建通信域 (AIV直驱模式无需配置hcclOpExpansionMode)
HcclCommConfig config;
HcclCommConfigInit(&config);
config.hcclWorldRankID = rank;
HcclCommInitRootInfoConfig(nranks, &rootInfo, rank, &config, &comm);

// 2. 注册通信内存
HcclCommMemReg(comm, "shareBuf", &regMem, &memHandle);

// 3. 获取链路endpoint
HcclRankGraphGetLinks(comm, layerId, rank, peerRank, &links, &linkNum);

// 4. 创建AIV直驱URMA P2P通道
channelDesc.remoteRank = peerRank;
channelDesc.channelProtocol = COMM_PROTOCOL_UBC_CTP; // URMA协议
channelDesc.memHandles = &memHandle;
channelDesc.memHandleNum = 1;
HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &channel); // 指定COMM_ENGINE_AIV

// 5. 获取对端内存地址
HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags);
```

#### 2. Kernel侧执行
通信流程分为三步：`Init()` → `WriteNbi()`/`ReadNbi()` → `Drain()`。各rank执行相同的Kernel逻辑，通过地址偏移区分三段数据：
- **seg0** `[0, DATA_SIZE)`：本卡pattern（Host预初始化）
- **seg1** `[DATA_SIZE, 2*DATA_SIZE)`：接收对端`WriteNbi`写入的数据
- **seg2** `[2*DATA_SIZE, 3*DATA_SIZE)`：接收本卡`ReadNbi`从对端seg0读回的数据

- **`Init`**：分配UB工作空间（>= 512字节），用于存放Hcomm内部WQE/CQE等状态。
- **`WriteNbi` / `ReadNbi`**：调用**AIV直驱URMA API**将通信任务入队到SQ。默认`commit=true`，入队后立即触发doorbell；如需批量优化，可改用`commit=false`多次入队后统一调用`Commit()`。
- **`Drain`**：轮询CQ等待通信任务完成，返回后数据可见性才有保证。

```cpp
// 单个rank上的AIV直驱URMA通信操作示例：
// 1. 本卡seg0 → 对端seg1 (WriteNbi)
hcomm_.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);

// 2. 对端seg0 → 本卡seg2 (ReadNbi)
hcomm_.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);

// 3. 等待AIV硬件通信任务完成
hcomm_.Drain(channel);
```

#### 3. 环形拓扑通信过程
运行时rank数量为`nranks`。每个rank计算相邻节点：
- `prev = (rank - 1 + nranks) % nranks`
- `next = (rank + 1) % nranks`

Host侧为`prev`和`next`分别准备通信上下文。当前样例的数据面只使用`next`方向：Kernel执行时，将本卡seg0通过`WriteNbi`写入`next`的seg1，并通过`ReadNbi`从`next`的seg0读到本卡seg2。因此，每个rank的seg1来自`prev`写入，seg2来自`next`读取。当`nranks == 2`时，`prev`和`next`都指向同一个对端rank，seg1和seg2会校验同一个对端pattern。

Host侧在Kernel执行前后通过TCP环形barrier同步所有rank，避免某个rank提前校验尚未完成的通信结果。

#### 4. 如何扩展为all-to-neighbor
当前样例已经建立了`prev`和`next`两个方向的P2P通道，但默认只执行`next`方向的数据面操作。若要扩展为完整的all-to-neighbor通信，可以复用已建立的`prev`通道，让每个rank同时覆盖“对前驱读”和“向后继写”两个方向。

一种直接实现方式是执行两次Kernel：第一次使用`prev`通道执行`ReadNbi`，从`prev`的seg0读到本卡seg2；第二次使用`next`通道执行`WriteNbi`，将本卡seg0写入`next`的seg1。这样每个rank的seg1由`prev`通过`WriteNbi`写入，seg2由本rank通过`ReadNbi`从`prev`读回，两段数据都应匹配`prev`的pattern。Host侧需要分别回读两次Kernel对应通信上下文中的`testResult`，并继续校验seg1和seg2的pattern。

扩展时建议保留Kernel前后的Host侧barrier：Kernel前确保所有rank完成内存注册、通道建立和context初始化；Kernel后确保所有rank完成对应方向的`Drain`后再回读`testResult`并校验数据。若新增或调整数据段偏移，需要同步更新Host侧pattern校验的期望rank和offset。

### 校验机制
- 每个rank在Host侧预初始化seg0时，通过线性同余生成器（LCG，使用Knuth乘法哈希常数`0x9E3779B9U`等参数）生成用于通信校验的随机pattern，确保不同rank的数据来源可区分。
- Kernel执行完毕后，Host侧通过`aclrtMemcpy`回读`CommContext::testResult`。
- Host侧进一步校验seg1（`prev`通过`WriteNbi`写入）和seg2（本卡通过`ReadNbi`从`next`读回）的数据是否与对应对端pattern完全一致。当校验结果码testResult为0（即seg1/seg2数据与对端pattern完全一致）时，打印test pass!。

## 编译与运行

在本样例根目录下执行如下步骤，编译并执行样例。

### 1. 配置环境变量
请根据当前环境上CANN开发套件包的安装方式，配置环境变量：
```bash
source ${install_path}/cann/set_env.sh
```
> **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`下。

### 2. 编译工程

在本样例目录下执行如下命令：
```bash
mkdir -p build && cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

### 3. 样例执行
本样例使用自动Fork多进程方式启动，命令格式如下：

```bash
./demo [nranks]
```

`nranks`表示启动的rank数量，必须大于等于2，并且不应超过当前可用NPU数量。不传入参数时，默认启动双卡：

```bash
# 默认双卡
./demo

# 显式指定双卡
./demo 2

# 指定4卡环形拓扑
./demo 4
```

### 4. 编译选项说明
| 选项 | 可选值 | 说明 |
|------|--------|------|
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU架构：`dav-3510`对应Ascend 950PR / Ascend 950DT |

### 5. 执行结果
多卡执行成功后，终端将输出如下信息，说明AIV直驱URMA通信成功（写入读出结果一致）：
```text
rank 0 test pass!
rank 1 test pass!
test pass!
```
> **注意：** 单卡环境仅支持编译验证，实际运行本样例需至少配备2张NPU。

## 开启 Hcomm 调试日志

调试日志通过编译宏 `ASCENDC_DEBUG` 控制，属于编译期选项，运行时不能切换。默认关闭。使用自己的用例时，除配置该选项外，还需要在用例的 `CMakeLists.txt` 中为实际的 target 添加宏定义：

```cmake
option(ASCENDC_DEBUG "Enable AscendC/Hcomm kernel debug logs" OFF)

if(ASCENDC_DEBUG)
    target_compile_definitions(<your_target> PRIVATE ASCENDC_DEBUG)
endif()
```

将 `<your_target>` 替换为用例实际的可执行文件或库名称。配置并重新编译：

```bash
rm -rf build
cmake -S . -B build -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DASCENDC_DEBUG=ON
cmake --build build -j
```

开启后，运行日志中会出现 `PostSend`、WQE、CQE 和 `PollCq` 等 Hcomm 调试信息，例如：

```text
[AIV Block 0/1] Hcomm URMA PostSend ...
[AIV Block 0/1] Hcomm URMA WQE: ...
[AIV Block 0/1] Hcomm URMA CQE: ...
```
