# MC2通算算子开发指南编写方案

## 1. 文档定位

拟新增正式指南：

```text
docs/zh/guide/mc2_operator_impl.md
```

最终指南沿用 [`ain_operator_impl.md`](https://github.com/Ascend/cann-asc-comm/blob/master/docs/zh/guide/ain_operator_impl.md) 的组织方式，围绕“概述—支持范围—总体流程—控制面资源准备—数据面通信执行—注意事项—样例”展开。

MC2 是代理模式的一类具体实现。指南使用两个层次描述它：控制面/数据面用于说明架构层次和资源生命周期；Host、AICore Client、AICPU_TS/CCU Server 用于说明代理模式中的具体执行角色。Host 准备资源并下发任务，AICore Kernel 承担 Client 角色，AICPU_TS 或 CCU 任务承担 Server 角色，Client 通过代理介质向 Server 提交通信请求，Server 驱动通信通路完成数据传输。

指南以 MC2 通信链路为主，计算部分只说明通信结果何时可消费、缓冲区何时可复用，以及计算 Kernel 如何接入通信结果。

正文直接讲 MC2 的通信对象、接口和执行阶段，不展示本方案内部的内容取舍。AllGather、AllReduce、ReduceScatter、AlltoAll、Add、MatMul 等只在代码示例中作为可替换实例。

## 2. 参考资料和支持范围

### 2.1 参考资料

最终指南与以下编程模型章节保持术语和流程一致：

| 资料 | 使用方式 |
| --- | --- |
| [`overview.md`](../programming_model/overview.md) | 对应控制面/数据面和资源准备、前同步、执行、后同步四阶段。 |
| [`communication_model.md`](../programming_model/communication_model.md) | 对应 Client、Server、通信引擎、通信域和代理模式。 |
| [`execution_model.md`](../programming_model/execution_model.md) | 对应 Prepare/Commit/Wait/Finalize、事务序和同步。 |
| [`memory_model.md`](../programming_model/memory_model.md) | 对应通信内存、地址范围、远端可见性和生命周期。 |
| [`communication_pattern_selection.md`](../programming_model/communication_pattern_selection.md) | 对应 MC2 代理路径中的通信引擎和协议选择。 |

计算实现参考 `/mnt/workspace/z00940346/asc-devkit/docs/zh`；旧版 HCCL 高阶 API 参考 `/mnt/workspace/z00940346/asc-devkit/docs/zh/api/SIMD-API/adv_api/HCCL_communication`，仅用于历史接口对照，当前签名和约束以当前 CANN/asc-comm 头文件及 API 参考为准。

代码示例以 `/mnt/workspace/z00940346/mc2_collective/main.asc` 和 `all_gather.asc` 为主，并使用以下 CCU 样例补充 Server 差异：

```text
/mnt/workspace/cx_asc-comm/asc-comm/examples/ccu/ccu_builtin/01_allgather
/mnt/workspace/cx_asc-comm/asc-comm/examples/ccu/ccu_builtin/02_allgather_add
/mnt/workspace/cx_asc-comm/asc-comm/examples/ccu/ccu_builtin/03_allreduce
```

### 2.2 支持范围

| 项目 | 计划说明 |
| --- | --- |
| 通信模式 | MC2 代理模式，AICore Client 与 AICPU_TS/CCU Server 协作。 |
| Host 侧 | ACL、HCCL 通信域、MC2 参数和 `OpResCtx/ccResCtx` 资源上下文。 |
| Device 侧 | AICore HCCL Client 的初始化、任务提交、Wait、同步和 Finalize。 |
| Server 路径 | `AICPU_TS` 与 `CCU_SCHED` 的启动差异。 |
| 通信操作 | 以通用任务参数描述，具体集合通信或点对点操作在示例中实例化。 |
| 计算阶段 | 只说明通信数据依赖和结果消费，具体计算引用 Ascend C 资料。 |
| 版本支持 | 使用当前仓库和 CANN 版本的产品标记，不把样例的卡数、核数和数据类型写成通用要求。 |

## 3. 总体流程

MC2 通算算子开发分为控制面资源准备和数据面通信执行两阶段。控制面由 Host 执行，负责准备资源和下发任务；数据面由 AICore Client 与 AICPU_TS/CCU Server 协作执行。

控制面流程：

1. 初始化 ACL，绑定 Device，创建 Client 和 Server 所需的独立执行 Stream/队列。
2. 获取并分发 `HcclRootInfo`，创建 HCCL 通信域。
3. 分配通信源/目的 GM 和计算缓冲区。
4. 通过 `Mc2GetCcArgs` 和 `Mc2SetCc*` 配置通信参数。
5. 通过 `Mc2AcquireCcResCtx` 获取 `OpResCtx/ccResCtx`。
6. 根据通信引擎启动 AICPU_TS 或 CCU Server。
7. 启动传入同一通信上下文的 AICore Client Kernel。
8. 等待 Client/Server 完成，回收结果并按创建逆序释放资源。

数据面流程：

1. AICore Client 使用 `InitV2` 初始化通信上下文。
2. 准备并提交通信任务，发布源数据就绪条件。
3. 在首次需要通信结果的位置调用 `Wait`。
4. 将已完成的通信结果交给计算阶段，按数据依赖复用缓冲区。
5. 所有参与核完成任务后执行核间同步并调用 `Finalize`。

核心时序用下面的通用关系表示：

```text
Host 控制面: HCCL domain -> MC2 args -> OpResCtx -> Server launch -> Client launch

AICore Client: InitV2 -> Prepare/Commit -> Wait -> consume result -> Finalize
AICPU_TS/CCU Server: receive request -> drive communication path -> publish completion
```

Client 和 Server 是两个独立的 Device 侧执行实体，Host 将它们提交到独立的 stream/任务队列中执行；Host 负责启动和同步，本身不直接执行 Device 侧通信任务。

## 4. 控制面资源准备

### 初始化 ACL 和 HCCL 通信域

说明 `aclInit`、`aclrtSetDevice`、Stream 创建，以及所有 Rank 使用同一份 `HcclRootInfo` 调用 `HcclCommInitRootInfo` 的必要性。`HcclComm`、通信域名称和 Rank 信息只做 MC2 资源申请所需的最小说明，并链接 [`communication_model.md`](../programming_model/communication_model.md)。

Host 示例以当前 `main.asc` 为基础，保留以下接口链路：

```cpp
HcclGetRootInfo(&rootInfo);
HcclCommInitRootInfo(rankSize, &rootInfo, rankId, &comm);
HcclGetCommName(comm, commName);
```

### 分配缓冲区和配置 MC2 参数

说明通信源/目的 GM、计算缓冲区和临时资源的用途，以及通信完成前的生命周期约束。随后介绍以下 MC2 接口的职责：

```cpp
Mc2GetCcArgs(&ccArgs);
Mc2SetCcGroupName(ccArgs, commName);
Mc2SetCcAlgConfig(ccArgs, algConfig);
Mc2SetCcSrcDataType(ccArgs, srcType);
Mc2SetCcDstDataType(ccArgs, dstType);
Mc2SetCcCommEngine(ccArgs, commEngine);
Mc2AcquireCcResCtx(comm, cmdType, ccArgs, &opResCtx, &opResCtxSize);
Mc2FreeCcArgs(ccArgs);
```

归约类型等操作相关参数只在对应通信操作的示例中出现。重点解释 `OpResCtx/ccResCtx` 是由 Host 准备、同时交给 Server 启动接口和 AICore Client 的设备侧资源上下文，必须保持同一份上下文和正确生命周期。

### 下发 Server 和 Client

用一个小节说明两条 Server 路径：

- `AICPU_TS`：通过 `Mc2SetCcCommEngine` 配置引擎，再由 `Mc2AicpuKernelLaunch` 在独立 Server Stream 上启动。
- `CCU_SCHED`：通过 `Mc2SetCcCommEngine` 配置 CCU 调度引擎，再由 `Mc2CcKernelLaunch` 启动 CCU Server；具体参数以当前 API 参考为准。

两条路径都把 `Mc2AcquireCcResCtx` 返回的上下文传给 AICore Client Kernel。说明 Server 先于 Client 进入可执行状态，Client 和 Server 使用独立执行队列，Kernel 完成后先同步 Client，再等待 Server 退出。

### 销毁通信资源

通信任务和结果消费完成后，按依赖逆序销毁：同步 Client/Server、释放 Device 缓冲区、销毁通信域和 Stream、Reset Device、Finalize ACL。`OpResCtx` 的释放方式以当前 MC2 资源管理契约为准，在正式指南中单独标明由谁管理。

## 5. 数据面通信执行：Client 与 Server

### Client Kernel 入口和初始化

以 `all_gather.asc` 的入口形式说明 AICore Client Kernel 参数：通信源/目的 GM、计算缓冲区、Host 传入的 `OpResCtx/ccResCtx` 和运行时通信参数。Kernel 内部调用：

```cpp
AscendC::InitSocState();
AscendC::Hccl<> hccl;
hccl.InitV2(mc2Context, nullptr);
```

这里的 `mc2Context` 必须是 Host 侧申请并传入的同一通信上下文。

### 提交通信任务

说明通信 API 的通用参数类别：源地址、目的地址、元素数量、数据类型、stride/offset、重复次数和操作相关参数。以当前样例的 `Hccl::AllGather<true>` 作为一个实例，展示如何把任务提交接口替换成其他通信操作；不在本节展开具体通信算法。

同时说明提交成功只代表请求进入 Client/Server 协作流程，不能据此读取目的缓冲区。`Prepare`、`Commit` 或同步模板参数的具体语义以当前 HCCL Client API 为准。

### Wait、计算消费和 Finalize

`Wait` 完成后，对应通信结果才进入可消费状态。AICore Client 可以在此处读取 GM 结果、搬运到计算侧工作区并执行任意计算；计算实现引用 Ascend C 资料，指南只保留数据依赖和内存复用边界。

所有通信任务和计算阶段完成后，多个 AIV 需要保持一致的调用顺序，必要时执行 `SyncAll`，最后调用 `Finalize` 通知 Server 当前 Client Kernel 已结束。

### 多核和分块约束

说明多 AIV 共同执行 Client 时必须保持通信任务顺序、Wait 次数、Finalize 时机和通信上下文访问规则一致。分块通信时，为每个分片定义源数据就绪、提交、完成和消费的依赖；分片数量、核数和 tile 大小只作为具体示例参数。

## 6. 注意事项和示例计划

正式指南保留以下通信侧注意事项：

- MC2 属于代理模式，Client 和 Server 是两个协作执行实体；通信上下文和 Server/Client 必须配对。
- Server 与 Client 的执行队列需要满足代理通信的调度要求，避免因队列依赖破坏 Client/Server 协作。
- 通信源/目的地址、长度单位、对齐、通信内存范围和访问授权必须符合当前协议契约。
- `Wait` 之前不能消费通信结果，通信完成前不能覆盖源数据或复用目的缓冲区。
- 多 AIV 的通信调用顺序、事务序、核间同步和资源使用必须一致。
- API 签名、通信引擎枚举、算法配置和产品支持范围以当前 CANN/asc-comm 版本为准。

示例计划分为三段：

1. **Host 通用骨架**：基于 `mc2_collective/main.asc`，展示通信域、MC2 参数、资源上下文、Server/Client 启动和资源销毁。
2. **AICore Client 骨架**：基于 `mc2_collective/all_gather.asc`，展示 `InitV2`、任务提交、Wait、计算结果消费和 Finalize。
3. **完整实例**：用当前 AllGather + Add 样例实例化前两段，并标注哪些参数和函数可以替换为其他通信操作与计算阶段。

方案内部的“哪些内容不写”只用于指导正式资料取舍，最终指南不展示这类过程说明；正式正文直接按上述通信流程组织。

正式指南完成后的检查只包括：编译级 API/代码示例校验、文档链接校验、术语一致性校验，以及在目标环境对 Server/Client 生命周期和通信结果进行基本功能验证。

## 7. 遗留问题

- `MC2` 的正式英文全称当前按 `Massive Communication Computing` 展开，仍需以产品命名规范或权威资料做最终确认；如确认结果不同，需要同步更新正式指南概述。
