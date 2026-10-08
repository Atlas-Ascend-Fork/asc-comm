# 多 Jetty SIMT 工程验收用例

本用例依赖 Hcomm 内部完成管理接口，用于工程验收，不作为用户 API 调用示例。它使用 `$ASCEND_HOME_PATH` 中已安装的 Hcomm 头文件；运行前安装与本仓库源码匹配的 run 包，并用 `--clean` 编译用例。

用例演示多个 Jetty 并行发布，以及由一个消费者统一处理共享 CQ、按 JFS 归属推进各 Jetty 的完成状态。支持 Write、Read 和 WriteWithNotify。

MakeBatchHandle 和每次批次 Write/Read/Notify 均显式传入 group。同一 handle 使用固定的 Group 和 UB 工作区。发布流程包含三个屏障，共享 CQ 由一个消费者统一管理。

## 执行

```bash
# 已加载 CANN 环境；每次只编一个 batch 的 VF。
bash build.sh --batch-size 1
# 默认运行 Write/Read/Notify × J1/2/4/8，共12项。
bash run.sh --batch-size 1
# 可选其中一项。
bash run.sh --batch-size 1 --api write --jetty-counts 4
# 对 8、32、64、128、256 分别重复上述两条，六组共72项。
```

固定64B、每Jetty1024计时请求、warmup96按batch向上对齐、两设备。`--batch-size` 同时指定每批请求数和每 Jetty 的 lane 数，每 lane 构造一个请求。`--jetty-counts` 指定 Jetty 数；lane 数和每 lane 请求数不能独立配置。

每组一个独立构建目录 `build/simt-matrix-bs<N>`，仅编译一个并行VF。`build.sh` 为 CMake 配置和编译设置300秒上限，超时即构建失败。构建成功后，脚本读取对应 batch/Jetty 几何的VF资源记录，要求stack不超过1152B，并记录寄存器用量。`--clean`仅清理选中的产物。

运行前核对 `Layout=matrix-v4`、batch及构建生成的二进制SHA。增量构建只有对象SHA相同时才复用资源记录。构建和执行参数以各脚本 `--help` 为准。

## 文件职责

| 文件 | 职责 |
|---|---|
| main.cpp | 分配缓冲、发起kernel、收集结果与释放资源 |
| host_setup.h | Jetty创建和远端内存索引解析 |
| simt_kernel.cpp | 单一并行VF与warmup/计时/Drain流程 |
| simt_submit.h | 三屏障发布顺序与每lane SQ维护 |
| simt_completion.h | CompletionSet初始化/接续、窗口登记、完成快照 |
| validation.h | 完成计数、全部payload字节、Notify值校验 |
| multi_jetty_batch_perf_common.h | 共享常量及主机/设备数据布局 |

每 lane 维护自己构造的 SQ，多个 Jetty 的 publisher 并行发布。三个 block 屏障协调发布前、发布后和下一批开始，组间会在这些边界等待；共享 CQ 的单消费者规则只约束完成管理，并不要求 Jetty 轮流提交。J8/BS256使用同一stream中的两个kernel，每个kernel包含4个Jetty；第二个kernel接续同一CompletionSet，不清零CQ游标。输出以Jettys和ConcurrentJettys区分配置数量与实际最大并行数量。

## 结果

每项同时要求双方进程成功、完成计数PASS和对应端数据校验通过；Notify还要求所有通知值通过。Read检查发起端，Write/Notify检查接收端，包含预热数据。

每组产生12条RESULT及一条MATRIX_SUMMARY。完整rank日志保留在汇总日志旁的`.ranks.*`目录。普通校验失败收集完本组；超时、信号、初始化错误停止后续项。没有执行的组合不能计为PASS。

与simt_batch_perf统一输出API、Mode、DataSize/B、BatchSize、Jettys、ConcurrentJettys、IssueTicksPerRequest、Pair及Status。主要指标为每请求下发耗时IssueTicksPerRequest，采用原始clock() ticks，包含提交路径同步，不包含随后Drain等待；多kernel累加提交区间，不包含host launch间隙。正常不展开完成快照和校验明细，所有检查仍执行，详情保留在rank日志，失败时输出。未对齐时钟口径前，不与归档SIMD直接计算加速比。

## RESULT 字段

| 字段 | 含义 |
|---|---|
| API | write、read、notify等通信操作 |
| Mode | Immediate=单lane直接提交；ExplicitBatch=单lane显式Batch；GroupBatch=多lane协作一个Jetty；MultiJetty=多个Jetty并行用例 |
| DataSize/B | 每个请求的数据字节数，不含Notify标志等控制信息 |
| BatchSize | 每个Jetty每批请求数；GroupBatch/MultiJetty每lane构造一项，因此也等于每Jetty的lane数 |
| Jettys | 该rank对配置的Jetty总数 |
| ConcurrentJettys | 单次kernel最多参与提交的Jetty数；例如J8/BS256为4，需要两个kernel |
| IssueTicksPerRequest | 提交计时区间总ticks除以总请求数，越小越好；原始clock()计数，未换算成ns |
| Pair | 通信rank对，例如0-1；8卡时有四对 |
| Status | 两端执行及该用例的正确性检查是否通过，PASS或FAIL |

IssueTicksPerRequest衡量平均下发成本，不是单个请求的端到端延迟。它包括构造/发布和提交路径同步，不包含后续Drain等待；不计预热，多kernel累加提交区间后除以总请求数，也不计host launch间隙。

成功构建不打印编译过程；正常运行只打印RESULT及已有汇总。编译输出保存在build目录的build-output.log，运行配置记录在汇总日志旁的`.metadata`文件。失败时自动显示编译日志末40行或对应rank日志；无需正常回传这些文件。
