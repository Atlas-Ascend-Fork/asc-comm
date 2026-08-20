# SIMT Notify/FAA/CAS 基础性能用例

## 点对点性能测试与 rank 说明

本样例测试**点对点**性能：固定由 Rank 0 作为发送方、Rank 1 作为接收方，测量单发送方到单接收方的
延迟和带宽。

`nranks` 支持 `>= 2`，可以传入 3 卡及以上。实际运行时 `run.sh` 会按 `nranks` 拉起对应数量的进程，
但**只有 Rank 0 和 Rank 1 参与测试**：

- Rank 0 / Rank 1：建立通信域（通信域只包含这两个 rank）和 UBC_CTP 通道，执行性能测试。Rank 0 计时
  并打印 `RESULT`，Rank 1 校验接收数据。
- Rank >= 2：打印 `SKIP` 后立即退出，不初始化通信域、不建链、不参与性能测试，也不影响测试结果。

只有发送方计时，可以避免多方并发发送带来的资源竞争和计时误差，点对点数据也更便于分析。如果需要多卡
通信或环形拓扑的功能验证，请使用 `examples/simt_notify_atomic`，它支持任意 `>= 2` 卡的环形拓扑通信。

需要注意 rank 号直接用作 device id（`aclrtSetDevice(rank)`），因此测试始终跑在 device 0 和 device 1
上；通道只采用第 0 层网络上的 `COMM_PROTOCOL_UBC_CTP` 链路，要求两卡间存在 UB 统一总线直连。

本目录只测试`Notify`、`FAA`和`CAS`三个接口。Immediate模式仅支持`--lanes 1`。Last模式允许
`--lanes 1/32/1024`：多个lane并发执行`commit=false`，同步后由一个lane执行唯一的`commit=true`。

当前性能 kernel 在计时开始前调用原有的 `Hcomm::Init(workspace, len)`。Hcomm 在不透明 UB
workspace 内自动为每个 lane 分配 128B WQE slot 和 32B 远端注册信息/token cache，并初始化所有
lane 共享只读的 128B channel/SQ post context。共享 context 在第一次业务调用取得 `channel` 后懒加载；
缓存只减少每个 WQE 重复解析 GM 元数据的开销，不改变 Immediate 提交语义，调用方不再管理三块
内部空间或分别调用绑定接口。

## 计时流程

三个用例使用完全相同的流程：

```text
执行 warmup 次接口
Drain                         不计时

begin = clock()
执行 iterations 次接口
issueEnd = clock()
Drain/PollCq
completionEnd = clock()
```

`IssueTime`和`AverageIssue`只统计正式WQE下发，用于比较接口提交时延；`CompletionTime`从正式下发开始
统计到`Drain/PollCq`结束，仅用于计算`CompletionBandwidth`。`DrainStatus`和`Completed`用于确认本次
下发最终正确完成。

## 末次提交用例

`--commit-mode last` 对一组 WQE 只执行一次最终提交：前 `iterations - 1` 个 WQE 使用
`commit=false` 写入普通 SQ，最后一个 WQE 使用 `commit=true` 通过 DWQE 发布整批，随后结束计时并
调用 `Drain()`。Last 只合并最终 DWQE 发布，SQ 空间仍按每个 WQE 执行一次预留 CAS。

`--lanes 1`串行填写延迟WQE；`--lanes 32/1024`由多个lane并行填写延迟WQE。两种情况都只由
一个lane执行最终`commit=true`和`Drain`。

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 \
  --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 faa \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 cas \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last
```

将上述命令的`--lanes`改为`32`或`1024`，可验证多lane Last路径。多lane不能用于Immediate模式。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_notify_atomic_perf/build.sh
```

## 分别运行三个接口

样例通过`HcclGetRootInfo`和`HcclCommInitRootInfo`创建通信域，无需准备rank table。`run.sh`会为每次运行创建临时root info文件，由rank 0写入、rank 1读取（rank >= 2已提前退出，不读取该文件）。

下面命令中的第一个参数是`nranks`。传`2`表示只拉起两个测试进程；传`4`会拉起四个进程，但仍只有rank 0和rank 1执行测试，rank 2和rank 3打印`SKIP`后退出。

Notify：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 --lanes 1
```

FAA：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 faa \
  --iterations 1024 --warmup 100 --lanes 1
```

CAS：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 cas \
  --iterations 1024 --warmup 100 --lanes 1
```

每条命令只执行一个接口和一个数据长度，不在脚本内部扫描或组合场景。Immediate模式必须使用
`--lanes 1`；多lane必须同时指定`--commit-mode last`。

## 输出

```text
RESULT | Path=SIMT-t1 | API=notify | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
STATUS | API=notify | Path=SIMT-t1 | CommitMode=immediate | PASS
```

- `IssueTime/us`：正式接口调用本身的总时间，不包含`Drain()`；
- `AverageIssue/ns`：`IssueTime / WqeCount`；
- `CompletionTime/us`：从正式下发开始到`Drain/PollCq`完成的总时间；
- `CompletionBandwidth/GB/s`：`DataSize × WqeCount / CompletionTime`；
- `DrainStatus=0` 且 `Completed=WqeCount`：本次用例完成。

FAA/CAS 的 `DataSize/B` 固定为 `8`。Notify 使用 `--payload-bytes` 指定的数据长度。

`nranks > 2` 时，未参与测试的rank会额外输出一行，不影响`RESULT`和`STATUS`的判定：

```text
SKIP | Rank=2 | Only Rank 0 and Rank 1 run the point-to-point benchmark.
```

## 汇总和绘图

脚本只统计`DrainStatus=0`且`Completed=WqeCount`的成功样本，并按`Path`、`API`、`CommitMode`和
`DataSize/B`分别聚合，避免混合Immediate和Last数据。

```bash
python3 examples/simt_notify_atomic_perf/summarize.py perf.log \
  --api notify --commit-mode all --all-sizes

python3 examples/simt_notify_atomic_perf/plot.py perf.log \
  --api notify --commit-mode all --metric latency -o notify_latency.svg

python3 examples/simt_notify_atomic_perf/plot.py perf.log \
  --api notify --commit-mode all --metric bandwidth -o notify_bandwidth.svg
```

延时图使用`AverageIssue/ns`，带宽图使用`CompletionBandwidth/GB/s`。FAA/CAS未显式指定
`--data-size`时，汇总脚本默认选择8B；Notify默认选择4096B。
