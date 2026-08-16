# SIMT Notify/FAA/CAS 基础性能用例

本目录只测试`Notify`、`FAA`和`CAS`三个接口。Immediate模式下，t1的每个WQE使用
`commit=true`；多lane按轮次协作预留和发布WQE，不包含阶段打点。

当不超过 32 个 lane 同时对同一 channel 执行 `commit=true` 时，Hcomm 会在接口内部自动合并
同一 warp 的 SQ 预留和 DWQE 发布：一个 lane 原子预留整组 SQ 空间，各 lane 并行填写自己的
WQE，最后一个有效lane发布该组。t1024超过单warp范围，因此每组显式调用`ReserveDwqeBatch`：
非发布lane以`commit=false`写SQ，block同步后仅由lane 0以`commit=true`发布该组。

当前性能 kernel 在计时开始前调用原有的 `Hcomm::Init(workspace, len)`。Hcomm 在不透明 UB
workspace 内自动为每个 lane 分配 128B WQE slot 和 32B 远端注册信息/token cache，并初始化所有
lane 共享只读的 128B channel/SQ post context。共享 context 在第一次业务调用取得 `channel` 后懒加载；
缓存只减少每个 WQE 重复解析 GM 元数据的开销，不改变 Immediate 提交语义，调用方不再管理三块
内部空间或分别调用绑定接口。

t1024 仅提供 1024 个 lane 所需的 128KiB WQE workspace，避免形成超过 128KiB 的单个静态 UB
对象；可选的远端缓存和共享 post context 不启用，Hcomm 自动回退到 GM 元数据解析。该配置先用于
区分 kernel 启动资源问题和跨 warp 提交问题，日志相应显示 `RemoteCache=Disabled`、`PostContext=GM`。

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

- t1 调用一次 `ReserveDwqeBurst`，整批只对 GM head 执行一次原子预留，并复用远端注册信息、
  SQ context 和 WQE 固定字段。
- t32/t1024 由各 lane 并行填写前面的 SQ WQE；所有 lane 同步后，由最后一个逻辑操作所属的 lane
  执行唯一一次 `commit=true`，其余 lane 等待发布完成。

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 \
  --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json faa \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last

bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json cas \
  --iterations 1024 --warmup 100 --lanes 1 --commit-mode last
```

将上述命令的 `--lanes` 改为 `32` 或 `1024`，即可验证对应的多 lane Last 路径。
默认值仍为 `--commit-mode immediate`，日志中显示实际的 `CommitMode`。

第 `lane` 个线程处理序号为 `lane, lane + laneCount, ...` 的WQE。t32由Hcomm透明执行单warp
合并；t1024以最多1024个WQE为一组协作提交。预热不足1024个WQE时，未提交WQE的lane仍参与
同步，但不占用SQ。全部lane返回后，由lane 0调用一次`Drain()`。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_notify_atomic_perf/build.sh
```

## 分别运行三个接口

Notify：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json notify \
  --iterations 1024 --warmup 100 --payload-bytes 4096 --lanes 1
```

FAA：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json faa \
  --iterations 1024 --warmup 100 --lanes 1
```

CAS：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
  2 /path/to/rank_table.json cas \
  --iterations 1024 --warmup 100 --lanes 1
```

将 `--lanes` 改为 `32` 或 `1024` 可运行同一接口的多 lane 用例。每条命令只执行一个
接口、一个 lane 配置和一个数据长度，不在脚本内部扫描或组合场景。

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
