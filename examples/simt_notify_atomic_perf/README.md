# SIMT Notify/FAA/CAS 基础性能用例

本目录只保留三个相互独立的性能用例：`Notify`、`FAA` 和 `CAS`。所有用例都由单个 lane 发起：一个
channel 必须由一个 lane 驱动，因此 `--lanes` 只接受 `1`。

当前性能 kernel 在计时开始前调用 `Hcomm::Init()`。SIMT 路径不需要 workspace：WQE 在 lane 私有存储
中暂存，每次提交都从全局内存解析 channel。

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
调用 `Drain()`。

延迟提交的 WQE 会额外预留一个 2BB DWQE 的空间，因为 SIMT 没有独立的 `Commit` 接口，发布整批的
DWQE 必须能放进同一个 SQ。

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

默认值为 `--commit-mode immediate`，日志中显示实际的 `CommitMode`。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_notify_atomic_perf/build.sh
```

## 分别运行三个接口

通信域通过 `HcclGetRootInfo` 和 `HcclCommInitRootInfo` 创建，因此不需要 rank table。`run.sh` 为每次
运行创建临时 root info 文件，rank 0 写入，其余 rank 读取。

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

每条命令只执行一个接口和一个数据长度，不在脚本内部扫描或组合场景。

## 输出

```text
RESULT | Path=SIMT-t1 | API=notify | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | RemoteCache=PerLaneUB | PostContext=SharedUB | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
```

- `IssueTime/us`：正式接口调用本身的总时间，不包含`Drain()`；
- `AverageIssue/ns`：`IssueTime / WqeCount`；
- `CompletionTime/us`：从正式下发开始到`Drain/PollCq`完成的总时间；
- `CompletionBandwidth/GB/s`：`DataSize × WqeCount / CompletionTime`；
- `DrainStatus=0` 且 `Completed=WqeCount`：本次用例完成。

FAA/CAS 的 `DataSize/B` 固定为 `8`。Notify 使用 `--payload-bytes` 指定的数据长度。

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
