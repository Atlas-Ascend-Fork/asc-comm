# CommRW SIMT Batch提交方式功能与下发性能样例

## 概述

本样例展示如何在Ascend C SIMT Kernel中，基于`asc::simt::asccomm`的Batch提交接口，验证六类URMA
接口（`write_nbi`、`read_nbi`、`write_with_notify_nbi`、`write_value_nbi`、`atomic_faa_nbi`、`atomic_cas_nbi`）的
功能正确性，并测量不同提交方式下WQE的设备侧下发耗时。样例采用rank配对拓扑（2卡直连一对；
8卡按0→1、2→3、4→5、6→7配成四对），每对独立建链、独立计时并输出结果。

样例覆盖两种提交方式：

- **ExplicitBatch**：单lane执行`Append → batch_commit`，batch size固定为1。
- **GroupBatch**：多lane每轮各构造一个请求，一次`batch_commit`可提交一轮或多轮请求。

Write GroupBatch支持每对rank并发使用多条通道。同一个SIMT线程块划分为多个协作组，每组使用一条通道、独立的UB工作区和收发缓冲区。
实际启动的线程数等于通道数乘以每组线程数。例如，3条通道每组使用32个线程时，实际启动96个线程，VF使用`__launch_bounds__(128)`。

主要指标`IssueTicksPerRequest`为计时提交区间的设备`clock()`差值除以请求数，越低越好。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构介绍

```text
06_simt_asccomm_urma_batch_perf
├── CMakeLists.txt            // 编译工程文件
├── simt_batch_perf.asc       // Host侧通信域创建、内存注册、用例调度与结果校验
├── op_kernel.cpp             // SIMT Kernel侧CommRW接口调用与下发计时
├── simt_batch_perf_common.h  // Host与Kernel共享定义
├── README.md                 // 中文样例说明
└── README_en.md              // 英文样例说明
```

## 样例描述

### 功能说明

六类接口的测试语义（SIMT路径，Kernel内直接下发WQE）：

| API | 数据流向 | 语义说明 |
|------|---------|------|
| `write_nbi` | 本地GM → 远端GM | 将本地Global Memory数据写入远端NPU指定地址。 |
| `read_nbi` | 远端GM → 本地GM | 从远端NPU指定地址读取数据到本地Global Memory。 |
| `write_with_notify_nbi` | 本地GM → 远端GM | 写入payload后，再写一个远端notify word。 |
| `write_value_nbi` | 本地WQE → 远端GM | 8字节值内联在WQE中直接写入远端，不读本地缓冲区。 |
| `atomic_faa_nbi` | 远端GM ↔ 本地GM | 对远端8字节word累加，fetch返回值写回本地。 |
| `atomic_cas_nbi` | 远端GM ↔ 本地GM | 对远端8字节word比较交换，fetch返回值写回本地。 |

两种提交方式：

| 提交方式 | 说明 |
|---------|------|
| ExplicitBatch | 单lane先`append_one_wqe`入队，再`batch_commit`统一发布，batch size固定为1。 |
| GroupBatch | 组内每个lane每轮各构造一个请求，由协作组统一提交已追加的请求。 |

数据校验策略：

- 每项用例同时检查设备返回状态（`drain`状态与完成计数）和落地数据。
- **read**检查本地落地的远端数据；**write/notify**检查接收端payload与notify值；
  **FAA/CAS**同时校验fetch返回值（发送端）与远端累加/交换结果（接收端）。
- 每个阶段的请求在64个slot中轮转，按slot校验。GroupBatch的预热和计时阶段使用独立的slot。

### 样例规格

| 项目 | 说明 |
|------|------|
| 通信模式 | 点对点配对通信（2卡一对；8卡按rank两两配对，各对并发执行） |
| 调用方式 | SIMT Kernel内`asc::simt::asccomm`接口下发 |
| 提交方式 | `compare`运行ExplicitBatch和GroupBatch，`batch`仅运行ExplicitBatch，`group`仅运行GroupBatch |
| 进程启动 | 可执行文件直接拉起，内部fork多rank |
| 支持rank数 | 2（一对）或8（四对），且不超过当前可用NPU数量 |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并
   创建通信域。每个用例由父进程独立拉起一轮rank进程。
2. 调用`HcclCommMemReg`向通信域注册本卡send/recv两个通信buffer，Channel创建时内存信息自动交换给对端。
3. 通过`HcclRankGraphGetLayers`和`HcclRankGraphGetLinks`获取对端链路Endpoint，调用
   `HcclChannelAcquire`一次申请所需数量的P2P通道（引擎`COMM_ENGINE_AIV`、协议`COMM_PROTOCOL_UB_CTP`），
   逐通道通过`HcclChannelGetRemoteMems`解析远端recv buffer的注册索引，再将通道表拷贝到设备。
4. Host预初始化send/recv数据（原子接口预置远端初值），barrier同步后发送方发起Kernel。
5. Kernel为每条通道分配独立的256字节UB工作区和独立的收发缓冲区切片，按提交方式下发预热和计时请求。
   GroupBatch中，所有组完成预热后同时进入计时阶段，各组独立追加和提交，所有组结束下发后各自调用
   `drain(batch)`等待完成。
6. 发送方回读计时与完成状态，Read/Atomic接口同时校验本地数据；接收方校验Write/Notify落地
   数据与原子远端结果。
7. 两端校验结果经控制通道allgather汇聚，由发送方输出该pair的RESULT行，父进程输出汇总结果。

```cpp
// 单lane Batch方式的最小调用序列（ExplicitBatch，batch size为1）：
constexpr uint32_t bytes = 256U; // 128B共享上下文 + 128B发布者DWQE镜像
alignas(128) __ubuf__ uint64_t workspace[bytes / sizeof(uint64_t)];
asc::simt::asccomm<> asccomm;
if (asccomm.init(nullptr, 0U) != 0) return;
auto batch = asccomm.make_batch_handle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes);
if (batch.context == nullptr) return;
if (asccomm.write_nbi(batch, remote, local, 64U) != 0) return;
if (asccomm.batch_commit(batch) != 0) return;
if (asccomm.drain(batch) != 0) return;
```

GroupBatch调用使用同一个不保存Group的asccomm对象，并在每轮任务追加和最终提交时显式传入group：

```cpp
// 以下调用由group所有lane参与；每lane每轮构造自己的一个请求。
auto batch = asccomm.make_batch_handle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, group);
if (batch.context == nullptr) return;
uint32_t rounds = 0U;
for (; rounds < 2U; ++rounds) {
    const bool first = rounds == 0U;
    if (asccomm.write_nbi(batch, first ? remote_for_lane : remote_for_lane_2,
            first ? local_for_lane : local_for_lane_2, 64U, group) != 0) {
        break;
    }
}
if (rounds == 0U || asccomm.batch_commit(batch, group) != 0) return;
if (group.thread_rank() == 0U) {
    asccomm.drain(batch);
}
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
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write
  ```

  常用执行方式示例：

  ```bash
  # 单通道默认运行ExplicitBatch和GroupBatch，GroupBatch分别使用16、32和128个lane。
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write
  # 单lane ExplicitBatch方式。
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --mode batch
  # 每对rank使用4条通道，每条通道对应一个32-lane协作组。
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --channels 4 --group-lanes 32
  # 单通道GroupBatch，组大小为2048 lane。
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --mode group --group-lanes 2048 --iterations 2048
  # 双通道Write GroupBatch，每组1024 lane。
  ./simt_batch_perf tcp://127.0.0.1:29626 2 write --channels 2 --group-lanes 1024
  # 指定接口、GroupBatch lanes。
  ./simt_batch_perf tcp://127.0.0.1:29626 2 cas --mode group --group-lanes 16,32,128
  # 六类接口全部执行，8卡四对并发。
  ./simt_batch_perf tcp://127.0.0.1:29626 8 all
  ```

- 启动参数说明

  ```bash
  ./simt_batch_perf <ip:port> <nranks> <write|read|notify|write_value|faa|cas|all> [options]
  ```

  | 参数 | 默认值 | 说明 |
  |------|--------|------|
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 启动的rank数量，支持2（一对）或8（四对），且不超过当前可用NPU数量 |
  | `<api>` | 无默认 | 测试的通信接口；`all`表示依次执行六类接口 |
  | `--mode` | 单通道为`compare`，多通道为`group` | 可选`compare`、`batch`、`group` |
  | `--channels` | `1` | 每对rank的通道数，取值1～32；多通道适用于`write`的`group`模式 |
  | `--iterations` | `1024` | 每条通道的计时请求数，至少为1。GroupBatch方式下须为组内lane数的整数倍 |
  | `--warmup` | `96` | 每条通道的预热请求数，设为0可跳过预热。GroupBatch方式下向上取整到完整的一轮 |
  | `--payload-bytes` | `64` | `write`、`read`和`notify`的payload大小，最小为2字节，上限为256 MiB，并受设备可用内存限制。`write_value`、`faa`和`cas`固定传输8字节 |
  | `--group-lanes` | `16,32,128`（1～8条通道），`16,32`（9～32条通道） | 每条通道对应的组大小，取值范围为16～2048。多通道模式下，组大小须为2的幂，且通道数与组大小的乘积不得超过2048 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  |------|--------|------|
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU架构：`dav-3510`对应Ascend 950PR&950DT系列产品 |

- 执行结果

  多卡执行成功后，终端将输出如下信息，每个rank pair每项一行，随后为汇总结果：

  ```text
  RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Publishes=1 | Pair=0-1 | Status=PASS
  RESULT | Example=simt_batch_perf | Status=PASS
  ```

  `IssueTicksPerRequest`的分子是最早开始下发至最后一次提交返回的设备`clock()`差值，分母是该rank对
  全部通道的计时请求总数。指标包含请求构造、提交、同步及队列满时分批回收的等待，单位为设备时钟计数。
  最后一次`drain`、预热和Host下发间隙在计时区间外。`Publishes`是所有通道在计时阶段成功提交的总次数。
  `BatchSize`表示每条通道每轮的请求数，`Jettys`和`ConcurrentJettys`均为每对rank
  使用的通道数，`Pair`是rank对。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- GroupBatch协作组至少包含16个线程，组内线程按硬件线程编号连续排列。组内rank为0的线程须位于硬件warp的lane 0或lane 16。
- GroupBatch按轮预留队列空间。每条通道的SQ和CQ容量须分别满足单轮WQE和CQE的空间需求。
- 批量句柄中的请求须访问所属通道的远端注册内存。同一批次内追加的请求须使用相同的WQE宽度。
- Group调用使用同一个不保存Group的asccomm对象，并在每轮任务追加和最终提交时显式传入group；同一handle
  从创建到提交必须使用同一Group（成员、rank及size保持一致）和同一UB工作区。无group重载用于
  单lane handle。write_nbi/read_nbi/write_with_notify_nbi/write_value_nbi/atomic_faa_nbi/atomic_cas_nbi均在末尾
  接收group入参，显式config模板参数位于group之前。
- `--payload-bytes`最小为2：`write_nbi`把payload拆成两段SGE，1字节会让第一段长度为0，
  而URMA不接受长度为0的SGE。
