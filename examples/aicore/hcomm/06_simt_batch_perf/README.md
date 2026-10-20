# Hcomm SIMT Batch提交方式功能与下发性能样例

## 概述

本样例展示如何在Ascend C SIMT Kernel中，基于`AscendC::simt::Hcomm`的Batch提交接口，验证六类URMA
接口（`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`WriteValueNbi`、`AtomicFAA`、`AtomicCAS`）的
功能正确性，并测量不同提交方式下WQE的设备侧下发耗时。样例采用rank配对拓扑（2卡直连一对；
8卡按0→1、2→3、4→5、6→7配成四对），每对独立建链、独立计时并输出结果。

样例覆盖三种提交方式：

- **Immediate**：单lane逐请求直接提交。
- **ExplicitBatch**：单lane执行`Append → BatchCommit`，batch size固定为1。
- **GroupBatch**：多lane各构造一个请求，每批一次`BatchCommit`；batch size等于lane数。

主要指标`IssueTicksPerRequest`为计时提交区间的设备`clock()`差值除以请求数，越低越好。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构介绍

```text
simt_batch_perf
├── CMakeLists.txt            // 编译工程文件
├── main.cpp                  // Host侧通信域创建、内存注册、用例调度与结果校验
├── op_kernel.cpp             // SIMT Kernel侧Hcomm接口调用与下发计时
├── simt_batch_perf_common.h  // Host与Kernel共享定义
├── README.md                 // 中文样例说明
└── README_en.md              // 英文样例说明
```

## 样例描述

### 功能说明

六类接口的测试语义（SIMT路径，Kernel内直接下发WQE）：

| API | 数据流向 | 语义说明 |
|------|---------|------|
| `WriteNbi` | 本地GM → 远端GM | 将本地Global Memory数据写入远端NPU指定地址。 |
| `ReadNbi` | 远端GM → 本地GM | 从远端NPU指定地址读取数据到本地Global Memory。 |
| `WriteWithNotifyNbi` | 本地GM → 远端GM | 写入payload后，再写一个远端notify word。 |
| `WriteValueNbi` | 本地WQE → 远端GM | 8字节值内联在WQE中直接写入远端，不读本地缓冲区。 |
| `AtomicFAA` | 远端GM ↔ 本地GM | 对远端8字节word累加，fetch返回值写回本地。 |
| `AtomicCAS` | 远端GM ↔ 本地GM | 对远端8字节word比较交换，fetch返回值写回本地。 |

三种提交方式：

| 提交方式 | 说明 |
|---------|------|
| Immediate | 每个请求单独走一次提交流程，`WriteNbi(batch)`等接口逐条下发。 |
| ExplicitBatch | 单lane先`Append`入队，再`BatchCommit`统一发布，batch size固定为1。 |
| GroupBatch | group内每个lane各构造一个请求，由最后一个publisher lane发布整批；batch size等于lane数。 |

数据校验策略：

- 每项用例同时检查设备返回状态（`Drain`状态与完成计数）和落地数据。
- **Read**检查本地落地的远端数据；**Write/Notify**检查接收端payload与notify值；
  **FAA/CAS**同时校验fetch返回值（发送端）与远端累加/交换结果（接收端）。
- 计时与预热请求轮转64个slot，避免都落在同一条cache line上；按slot而非按操作校验。

### 样例规格

| 项目 | 说明 |
|------|------|
| 通信模式 | 点对点配对通信（2卡一对；8卡按rank两两配对，各对并发执行） |
| 调用方式 | SIMT Kernel内`AscendC::simt::Hcomm`接口下发 |
| 提交方式 | `compare`（默认，Immediate+GroupBatch对照）、`immediate`、`batch`、`group` |
| 进程启动 | 可执行文件直接拉起，内部fork多rank |
| 支持rank数 | 2（一对）或8（四对），且不超过当前可用NPU数量 |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并
   创建通信域。每个用例由父进程独立拉起一轮rank进程。
2. 调用`HcclCommMemReg`向通信域注册本卡send/recv两个通信buffer，Channel创建时内存信息自动交换给对端。
3. 通过`HcclRankGraphGetLayers`和`HcclRankGraphGetLinks`获取对端链路Endpoint，调用
   `HcclChannelAcquire`创建P2P通道（引擎`COMM_ENGINE_AIV`、协议`COMM_PROTOCOL_UBC_CTP`），
   并通过`HcclChannelGetRemoteMems`解析远端recv buffer的注册索引。
4. Host预初始化send/recv数据（原子接口预置远端初值），barrier同步后发送方发起Kernel。
5. Kernel侧执行`Init()`分配UB工作区后，按提交方式下发warmup与计时请求：连续请求轮转64个slot，
   最后`Drain()`（Immediate为`Drain(channel)`，Batch方式为`Drain(batch)`）等待完成。
6. 发送方回读计时与完成状态，Read/Atomic接口同时校验本地数据；接收方校验Write/Notify落地
   数据与原子远端结果。
7. 两端校验结果经控制通道allgather汇聚，由发送方输出该pair的RESULT行，父进程输出汇总结果。

```cpp
// 单lane Batch方式的最小调用序列（ExplicitBatch，batch size为1）：
constexpr uint32_t bytes = 256U; // 128B共享上下文 + 128B发布者DWQE镜像
alignas(128) __ubuf__ uint64_t workspace[bytes / sizeof(uint64_t)];
AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0U) != 0) return;
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remote, local, 64U) != 0) return;
if (hcomm.BatchCommit(batch) != 0) return;
if (hcomm.Drain(batch) != 0) return;
```

GroupBatch调用使用同一个不保存Group的Hcomm对象，并在每次批次操作末尾显式传入group：

```cpp
// 以下调用由group所有lane参与；每lane构造自己的一个请求。
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U, group);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remoteForLane, localForLane, 64U, group) != 0) return;
if (hcomm.BatchCommit(batch, group) != 0) return;
// 所有posting lane返回后，由一个lane调用Drain；Drain不接收group。
```

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  样例编译时使用已安装到CANN目录的asc-comm头文件。若尚未安装，先在仓库根目录生成并安装开发验证
  run包，参数说明参见[`docs/zh/compilation/build_and_test.md`](../../../../docs/zh/compilation/build_and_test.md)：

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_9.3.0_linux-<arch>.run --full
  ```

  然后加载CANN环境变量：

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
  cd .. && ./build/simt_batch_perf tcp://127.0.0.1:29626 2 write
  ```

  常用执行方式示例：

  ```bash
  # 默认compare模式：Immediate与GroupBatch（2/4/8/16/32/128 lanes）对照。
  ./build/simt_batch_perf tcp://127.0.0.1:29626 2 write
  # 单lane ExplicitBatch方式。
  ./build/simt_batch_perf tcp://127.0.0.1:29626 2 write --mode batch
  # 指定接口、GroupBatch lanes。
  ./build/simt_batch_perf tcp://127.0.0.1:29626 2 cas --mode group --group-lanes 16,32,128
  # 六类接口全部执行，8卡四对并发。
  ./build/simt_batch_perf tcp://127.0.0.1:29626 8 all
  ```

- 启动参数说明

  ```bash
  ./build/simt_batch_perf <ip:port> <nranks> <write|read|notify|write_value|faa|cas|all> [options]
  ```

  | 参数 | 默认值 | 说明 |
  |------|--------|------|
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 启动的rank数量，支持2（一对）或8（四对），且不超过当前可用NPU数量 |
  | `<api>` | 无默认 | 测试的通信接口；`all`表示依次执行六类接口 |
  | `--mode` | `compare` | `compare`（Immediate+GroupBatch对照）、`immediate`、`batch`、`group` |
  | `--iterations` | `1024` | 计时请求数；GroupBatch方式下须整除lane数 |
  | `--warmup` | `96` | 预热请求数；GroupBatch方式下向完整batch上取整 |
  | `--payload-bytes` | `64` | `write`/`read`/`notify`的payload字节数，最小为2；`write_value`、`faa`、`cas`恒为8字节，三者忽略该选项 |
  | `--group-lanes` | `2,4,8,16,32,128` | GroupBatch方式的lane数列表（逗号分隔），即每批请求数 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  |------|--------|------|
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU架构：`dav-3510`对应Ascend 950PR&950DT系列产品 |
  | `SIMT_BATCH_ENABLE_LARGE_GROUP` | `OFF`（默认）/`ON` | 是否编译1024-lane GroupBatch支持路径；lane数超过128时需以`-DSIMT_BATCH_ENABLE_LARGE_GROUP=ON`重新编译 |

- 执行结果

  多卡执行成功后，终端将输出如下信息，每个rank pair每项一行，随后为汇总结果：

  ```text
  RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
  RESULT | Example=simt_batch_perf | Status=PASS
  ```

  主要指标`IssueTicksPerRequest`=计时提交区间的设备`clock()`差值/请求数，越低越好。包含构造、
  提交及提交路径同步，不包含随后`Drain`的等待；单位为原始ticks，未换算为ns。`BatchSize`是每批
  请求数；`Jettys`和`ConcurrentJettys`分别是配置数量和实际最大并行数量，本样例单Jetty两者均为1；
  `Pair`是rank对。只比较PASS行，不与未对齐时钟口径的SIMD数据直接计算加速比。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- BatchHandle绑定`remote`所在的远端注册区，后续目标地址必须位于同一区域；`MakeBatchHandle`
  最后的`1U`表示普通Write的每项BB数。
- Group调用使用同一个不保存Group的Hcomm对象，并在每次批次操作末尾显式传入group；同一handle
  从创建到提交必须使用同一Group（成员、rank及size保持一致）和同一UB工作区。无group重载用于
  单lane handle；size=1的Group使用单lane分派。Write/Read/Notify/WriteValue/FAA/CAS均在末尾
  接收group入参，显式config模板参数位于group之前。
- `IssueTicksPerRequest`衡量平均下发成本，不是单个请求的端到端延迟：包括构造/发布和提交路径
  同步，不包含后续`Drain`等待；不计预热，也不计host launch间隙。
- `--payload-bytes`最小为2：立即提交的`WriteNbi`把payload拆成两段SGE，1字节会让第一段长度为0，
  而URMA不接受长度为0的SGE。
