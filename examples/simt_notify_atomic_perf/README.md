# SIMT Notify/FAA/CAS 基础性能用例

本目录只保留三个相互独立的性能用例：`Notify`、`FAA` 和 `CAS`。t1 每个 WQE 使用
`commit=true`；多 lane 使用一次合法的协作提交，不包含阶段打点。

当不超过 32 个 lane 同时对同一 channel 执行 `commit=true` 时，Hcomm 会在接口内部自动合并
同一 warp 的 SQ 预留和 DWQE 发布：一个 lane 原子预留整组 SQ 空间，各 lane 并行填写自己的
WQE，最后一个有效 lane 发布整组。t1024 超过单 warp 范围，因此显式调用 `ReserveDwqeBatch`：
非发布 lane 以 `commit=false` 写 SQ，block 同步后仅由 lane 0 以 `commit=true` 写一次 DWQE。

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
end = clock()
Drain                         不计时，仅校验完成状态
```

因此日志中的时间只包含正式 WQE 下发，不包含最终 `Drain/PollCq`，对应 Issue 指标。
`DrainStatus` 和 `Completed` 仍用于确认本次下发最终正确完成。

## 末次提交用例

`--commit-mode last` 使用单 lane 执行一组 WQE：前 `iterations - 1` 次调用使用
`commit=false`，最后一次调用使用 `commit=true`，随后结束计时并调用 `Drain()`。
每轮提交前调用一次 `ReserveDwqeBurst`，整批只对 GM head 执行一次原子预留，并缓存本批次
复用的远端注册信息、SQ context 和 WQE 固定字段；中间 WQE 直接写普通 SQ，最后一个 WQE
通过 DWQE 发布整批。该模式用于验证并测量单 lane 末次统一提交路径。

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

默认值为 `--commit-mode immediate`，现有命令行为不变。末次提交模式当前只支持
`--lanes 1`，日志中显示 `CommitMode=Last`。

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
RESULT | Path=SIMT-t1 | API=notify | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | RemoteCache=PerLaneUB | PostContext=SharedUB | IssueTime/us=... | AverageIssue/ns=... | IssueBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
```

- `IssueTime/us`：正式接口调用本身的总时间，不包含 `Drain()`；
- `AverageIssue/ns`：`IssueTime / WqeCount`；
- `IssueBandwidth/GB/s`：`DataSize / AverageIssue`；
- `DrainStatus=0` 且 `Completed=WqeCount`：本次用例完成。

FAA/CAS 的 `DataSize/B` 固定为 `8`。Notify 使用 `--payload-bytes` 指定的数据长度。
