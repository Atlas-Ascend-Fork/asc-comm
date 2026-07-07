# hcomm_write_read_nbi样例

## 概述

本样例展示如何在Ascend C kernel中使用`Hcomm`类的`WriteNbi`和`ReadNbi`接口实现NPU间点对点（P2P）通信，并配套完整的通信域创建流程。两卡对称执行`WriteNbi`+`ReadNbi`，通过地址偏移区分数据段，相互写入并读出数据，最后校验结果一致。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
├── hcomm_write_read_nbi
│   ├── CMakeLists.txt              // 编译工程文件
│   ├── README.md                   // 样例说明文档
│   └── hcomm_write_read_nbi.asc    // Ascend C样例实现（Kernel + Host）
```

## 样例描述

- 样例功能：

  在两张卡上对称演示P2P通信接口：每张卡同时执行`WriteNbi`和`ReadNbi`，通过地址偏移将通信buffer分为三段（seg0/seg1/seg2），实现两卡互写互读。与集合通信（如`AlltoAll`）不同，P2P通信直接向目标NPU发起RDMA操作，无需等待所有rank同步，延迟更低，适用于MoE Dispatch/Combine、Pipeline并行等场景。

  | 接口 | 数据流向 | 语义 |
  |------|---------|------|
  | `WriteNbi` | 本地→远端 | 将本地GM数据写入远端NPU指定地址 |
  | `ReadNbi` | 远端→本地 | 从远端NPU指定地址读取数据到本地GM |

- 样例实现：

  - Host侧通信域准备

    950上通信域需多进程方式创建：每个进程对应一个rank，通过TCP交换rootInfo后各自调用`HcclCommInitRootInfoConfig`。关键步骤：

    1. rank 0调用`HcclGetRootInfo`获取root信息，通过TCP发送给rank 1。
    2. 各rank各自调用`HcclCommInitRootInfoConfig`创建通信域。
    3. `HcclCommMemReg`向通信域注册本卡的通信buffer，channel创建时将该内存信息交换给对端。
    4. `HcclRankGraphGetLayers`+`HcclRankGraphGetLinks`获取本rank到对端的链路endpoint信息。
    5. `HcclChannelAcquire`创建到对端的P2P通道，指定远端rank、协议（`COMM_PROTOCOL_UBC_CTP`对应URMA）、endpoint、通知数量和待交换内存句柄。
    6. `HcclChannelGetRemoteMems`获取对端注册的内存地址，作为kernel侧`WriteNbi`/`ReadNbi`的远端地址。
    7. Host预初始化seg0（填充基于`rankId`的pattern），将`ChannelHandle`和buffer地址封装到`CommContext`下发到各卡GM。

    ```cpp
    // 多进程：rank 0生成rootInfo通过TCP发送，rank 1接收
    HcclGetRootInfo(&rootInfo);  // rank 0
    // 各rank各自创建通信域
    HcclCommConfigInit(&config);
    config.hcclWorldRankID = rank;
    HcclCommInitRootInfoConfig(nranks, &rootInfo, rank, &config, &comm);
    // 注册通信内存
    HcclCommMemReg(comm, "shareBuf", &regMem, &memHandle);
    // 获取链路endpoint
    HcclRankGraphGetLinks(comm, layerId, rank, peerRank, &links, &linkNum);
    // 创建P2P通道
    channelDesc.notifyNum = 3;
    HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &channel);
    // 获取对端内存地址
    HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags);
    ```

  - Kernel侧执行

    通信流程分为三步：`Init()`→`WriteNbi()`/`ReadNbi()`→`Drain()`。两卡执行相同的kernel逻辑，通过地址偏移区分三段：

    - seg0 `[0, DATA_SIZE)`：本卡pattern（Host预初始化）
    - seg1 `[DATA_SIZE, 2*DATA_SIZE)`：接收对端`WriteNbi`写入的数据
    - seg2 `[2*DATA_SIZE, 3*DATA_SIZE)`：接收本卡`ReadNbi`从对端seg0读回的数据

    - `Init`：分配UB工作空间（>=512字节），存放WQE/CQE等内部状态。
    - `WriteNbi`/`ReadNbi`：将通信任务入队到SQ。默认`commit=true`入队后立即doorbell；如需批量优化可改用`commit=false`多次入队后统一`Commit()`。
    - `Drain`：轮询CQ等待通信任务完成，返回后数据可见性才有保证。

    ```cpp
    // 两卡对称执行：本卡seg0 → 对端seg1，对端seg0 → 本卡seg2
    hcomm_.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);
    hcomm_.ReadNbi(channel, localBuf + 2*DATA_SIZE, remoteBuf, DATA_SIZE);
    hcomm_.Drain(channel);
    ```

  - 调用实现

    多进程对称执行：两卡同时launch kernel，无需分阶段同步。Host侧预初始化seg0后通过`TcpBarrier`确保两卡就绪，再各自调用kernel。使用内核调用符`<<<>>>`调用核函数。

- 校验机制：

  - Host预初始化seg0时填充基于`rankId`的确定性pattern（`(rankId + 1) & 0xFF`），通信后可区分数据来源。
  - Kernel校验seg1（对端`WriteNbi`写入）和seg2（本卡`ReadNbi`读回）是否都为对端pattern，结果写入`CommContext::testResult`。
  - Host侧通过`aclrtMemcpy`回读`testResult`，为0时打印`test pass!`。

## 编译运行

在本样例根目录下执行如下步骤，编译并执行样例。

- 配置环境变量

  请根据当前环境上CANN开发套件包的[安装方式](../../../../../docs/quick_start.md#prepare&install)，配置环境变量。

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}` 为CANN包安装目录，未指定安装目录时默认安装至 `/usr/local/Ascend` 下。

- 样例执行

  在本样例目录下执行如下命令。

  ```bash
  mkdir -p build && cd build;                                               # 创建并进入build目录
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..;make -j;                      # 编译工程
  ```

  本样例为多进程方式，直接执行即可自动fork两个子进程（两卡对称执行WriteNbi+ReadNbi）：

  ```bash
  ./demo
  ```

  也可以手动指定参数单独运行某个rank：

  ```bash
  # 终端1：启动rank 0（卡0）
  ./demo 0 2 tcp://127.0.0.1:29621

  # 终端2：启动rank 1（卡1）
  ./demo 1 2 tcp://127.0.0.1:29621
  ```

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  |------|--------|------|
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU架构：dav-3510对应Ascend 950PR/Ascend 950DT |

- 执行结果

  执行结果如下，说明通信成功（两卡对称写入读出、结果一致）。

  ```text
  rank 0 test pass!
  rank 1 test pass!
  test pass!
  ```

  单卡环境仅支持编译验证，运行时需至少2张NPU。
