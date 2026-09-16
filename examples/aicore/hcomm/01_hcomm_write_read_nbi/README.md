# Hcomm AIV直驱URMA WriteNbi/ReadNbi点对点通信样例

## 概述

本样例展示如何在Ascend C AIV Kernel中，基于**AIV直驱URMA**架构，使用`Hcomm`类的`WriteNbi`和
`ReadNbi`接口实现NPU间低时延的点对点（P2P）通信。样例采用多rank环形拓扑：每个rank与前后相邻rank
建立P2P通道，通过地址偏移区分数据段，Kernel侧直接提交通信任务，最后在Host侧校验通信结果。

Host侧只负责创建通信域、注册通信内存和建立通道；通信任务由AIV Kernel直接提交，数据面执行期间
不需要Host逐次参与。该模式适用于MoE Dispatch/Combine、Pipeline并行等对通信时延敏感的场景。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
hcomm_write_read_nbi
├── CMakeLists.txt              // 编译工程文件
├── hcomm_write_read_nbi.asc    // Host侧资源准备与Kernel调用
├── hcomm_write_read_nbi_kernel.cpp // AIV Kernel侧Hcomm调用
├── hcomm_rw_def.h              // Host与Kernel共享定义
├── README.md                   // 中文样例说明
└── README_en.md                // 英文样例说明
```

## 样例描述

### 功能说明

| API | 数据流向 | 语义说明（AIV直驱URMA） |
|------|---------|------|
| `WriteNbi` | 本地GM → 远端GM | AIV直驱URMA写接口：将本地Global Memory数据直接写入远端NPU指定地址，无需远端CPU介入。 |
| `ReadNbi` | 远端GM → 本地GM | AIV直驱URMA读接口：直接从远端NPU指定地址读取数据到本地Global Memory，无需远端CPU介入。 |

每个rank的通信缓冲区划分为三段，通过地址偏移区分：

- **seg0** `[0, DATA_SIZE)`：本卡pattern（Host侧以`rank + index`模式预初始化，首字节为rankId，确保不同rank的数据来源可区分）
- **seg1** `[DATA_SIZE, 2*DATA_SIZE)`：接收`prev`通过`WriteNbi`写入的数据
- **seg2** `[2*DATA_SIZE, 3*DATA_SIZE)`：接收本卡通过`ReadNbi`从`next`读回的数据

Kernel执行完毕后，Host侧回读结果并校验seg1和seg2是否与对端pattern完全一致。

### 样例规格

| 项目 | 说明 |
|------|------|
| 通信模式 | 环形拓扑AIV直驱URMA点对点通信 |
| 调用方式 | AIV Kernel内`Hcomm::WriteNbi`/`ReadNbi` |
| 进程启动 | 可执行文件直接拉起，内部fork多rank |
| 支持rank数 | >= 2，且不超过当前可用NPU数量 |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并
   创建通信域。**注意：AIV直驱模式下，无需配置`hcclOpExpansionMode`。**
2. 调用`HcclCommMemReg`向通信域注册本卡通信buffer，Channel创建时内存信息自动交换给对端。
3. 通过`HcclRankGraphGetLayers`和`HcclRankGraphGetLinks`获取到`prev`和`next`的链路Endpoint。
4. 调用`HcclChannelAcquire`创建P2P通道，指定引擎为`COMM_ENGINE_AIV`、协议为`COMM_PROTOCOL_UB_CTP`（URMA协议）。
5. 调用`HcclChannelGetRemoteMems`获取对端注册内存地址，作为Kernel侧`WriteNbi`/`ReadNbi`的远端目标地址。
6. Host预初始化seg0，将`ChannelHandle`和buffer地址封装到`CommContext`下发到各卡GM。
7. Kernel侧执行`Init()`分配UB工作空间后提交通信任务：将本卡seg0通过`WriteNbi`写入`next`的
   seg1，并通过`ReadNbi`从`next`的seg0读到本卡seg2，最后`Drain()`等待通信任务完成。
8. Host侧通过控制通道barrier同步后，回读结果并校验seg1和seg2的pattern。

```cpp
// 单个rank上的AIV直驱URMA通信操作示例：
// 1. 本卡seg0 → 对端seg1 (WriteNbi)
hcomm_.WriteNbi(channel, remoteBuf + DATA_SIZE, localBuf, DATA_SIZE);

// 2. 对端seg0 → 本卡seg2 (ReadNbi)
hcomm_.ReadNbi(channel, localBuf + 2 * DATA_SIZE, remoteBuf, DATA_SIZE);

// 3. 等待AIV硬件通信任务完成
hcomm_.Drain(channel);
```

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的安装方式配置环境变量。

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  在本样例目录下执行如下命令。可执行文件内部fork出全部rank进程，直接运行即可：

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/hcomm_write_read_nbi tcp://127.0.0.1:29623 <nranks>
  ```

- 启动参数说明

  | 参数 | 默认值 | 说明 |
  |------|--------|------|
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 启动的rank数量，必须 >= 2，且不超过当前可用NPU数量 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  |------|--------|------|
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU架构：`dav-3510`对应Ascend 950PR/Ascend 950DT |

- 执行结果

  多卡执行成功后，终端将输出如下信息，说明AIV直驱URMA通信成功（写入读出结果一致）：

  ```text
  [rank 0] hcomm_write_read_nbi | sent to rank 1, received from rank 1 | PASS
  [rank 1] hcomm_write_read_nbi | sent to rank 0, received from rank 0 | PASS
  RESULT | Example=hcomm_write_read_nbi | Status=PASS
  ```

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- Kernel侧`Init()`分配的UB工作空间需 >= 512字节，用于存放Hcomm内部WQE/CQE等状态。
- `WriteNbi`/`ReadNbi`默认`commit=true`，入队后立即触发doorbell；如需批量优化，可改用
  `commit=false`多次入队后统一调用`Commit()`。`Drain()`返回后数据可见性才有保证。
- 当`nranks == 2`时，`prev`和`next`指向同一个对端rank，seg1和seg2会校验同一个对端pattern。
- 当前样例已建立`prev`和`next`两个方向的P2P通道，但默认只执行`next`方向的数据面操作。若要
  扩展为完整的all-to-neighbor通信，可复用已建立的`prev`通道：执行两次Kernel，第一次使用`prev`
  通道执行`ReadNbi`从`prev`的seg0读到本卡seg2，第二次使用`next`通道执行`WriteNbi`将本卡seg0
  写入`next`的seg1，使每个rank的seg1和seg2都匹配`prev`的pattern。扩展时需保留Kernel前后的
  Host侧barrier；若调整数据段偏移，需同步更新Host侧pattern校验的期望rank和offset。
