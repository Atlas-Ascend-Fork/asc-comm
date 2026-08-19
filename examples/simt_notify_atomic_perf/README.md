# SIMT Notify/FAA/CAS 基础性能用例

## ⚠️ 重要说明：仅支持 2 卡

本样例是**点对点性能基准测试**，设计为固定的 Rank 0（发送方）→ Rank 1（接收方）架构，**仅支持 2 卡配置**。

### 为什么只支持 2 卡？

1. **性能测量目标**：本样例测量单个发送方到单个接收方的点对点延迟和带宽，这是最基础的性能指标
2. **计时准确性**：只有发送方（Rank 0）执行计时，避免多方同时发送带来的资源竞争和计时误差
3. **结果可解释性**：点对点场景下的性能数据具有明确的物理意义，便于分析和优化

### 如果需要多卡通信

如果您需要测试多卡通信或环形拓扑，请使用：
- **功能验证**：`examples/simt_notify_atomic` - 支持任意 ≥2 卡的环形拓扑通信
- **多对通信**：可以基于本样例扩展，但需要重新设计计时和聚合逻辑

---

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

样例通过`HcclGetRootInfo`和`HcclCommInitRootInfo`创建通信域，无需准备rank table。`run.sh`会为每次运行创建临时root info文件，由rank 0写入、其余rank读取。

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
