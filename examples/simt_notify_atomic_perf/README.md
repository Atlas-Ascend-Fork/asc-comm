# SIMT Notify 单 lane 下发性能测试

本样例统计单 QP、单 lane 连续执行 `WriteWithNotifyNbi<true>` 时，“填写 WQE + 敲 DB”的耗时。预热后的 `Drain` 和正式测试后的 `Drain/PollCq` 均位于计时区间外。

当前版本优先保证性能计时建立在已验证的功能路径上，因此仅支持：

- 接口：`Notify`
- 路径：`SIMT`
- lane 数：`t=1`
- 提交方式：每个 WQE 使用 `commit=true`，每次提交均敲一次 DB，最后统一 `Drain`

多 lane 分组提交、FAA/CAS 性能测试和 AIV 基线将在该路径稳定后分别补充。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_notify_atomic_perf/build.sh
```

`build.sh` 会先清理本样例的 `build` 目录，再重新配置和编译。

## 快速功能验证

先使用 8B 数据、100 次预热和 1 次正式提交确认执行流程：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
    2 /path/to/rank_table.json notify \
    --loop-count 1 \
    --warmup 100 \
    --threads 1 \
    --exponent-range 3 3 \
    --simt-only
```

预期 rank 0 和 rank 1 均输出“性能用例通过”，且 `RESULT` 中：

```text
DrainStatus=0 | Completed=1
```

## 性能运行

扫描 8B 到 128KiB，每个长度正式提交 1024 个 WQE，并预热 100 个 WQE：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
    2 /path/to/rank_table.json notify \
    --loop-count 1024 \
    --warmup 100 \
    --threads 1 \
    --exponent-range 3 17 \
    --simt-only
```

只测试 4096B：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
    2 /path/to/rank_table.json notify \
    --loop-count 1024 \
    --warmup 100 \
    --threads 1 \
    --exponent-range 12 12 \
    --simt-only
```

脚本默认将完整输出保存到 `results/notify_<时间>.log`，也可以通过 `--log FILE` 指定路径。`--simt-only` 在当前版本中为兼容参数，因为当前阶段本身只运行 SIMT。

## 输出指标

普通运行提示使用中文。为了保持绘图脚本兼容，机器可读的 `RESULT` 行继续使用英文键名：

```text
RESULT | Path=SIMT-t1 | API=notify | DataSize/B=4096 | Npus=2 | WqeCount=1024 | DoorbellCount=1024 | Warmup=100 | TotalTime/us=5375.956000 | AverageWqe/ns=5249.957031 | PacketRate/Mpps=0.190478 | IssueBandwidth/GB/s=0.780197 | DrainStatus=0 | Completed=1024
```

- `TotalTime/us`：填写全部 WQE 并敲 DB 的总耗时。
- `AverageWqe/ns = TotalTime / WqeCount`：平均单个 WQE 的下发时延。
- `PacketRate/Mpps = WqeCount / TotalTime(us)`：每秒百万 WQE 数。
- `IssueBandwidth/GB/s = DataSize(B) / AverageWqe(ns)`：软件下发带宽，不是网络完成带宽。
- `DoorbellCount`：当前 `t=1` 路径等于 `WqeCount`。
- `DrainStatus=0` 且 `Completed=WqeCount`：计时后的统一完成检查成功。

## 已验证结果与当前性能差异

当前版本已在同一测试环境完成8B～128KiB扫描，每个数据长度均满足：

```text
DrainStatus=0 | Completed=1024
```

4096B下发1024个WQE的实测总耗时约为`5376us`、包率约为`0.190 Mpps`。参考AIN优化版本约为`970us`、`1.055 Mpps`，当前版本慢约5.5倍。

主要代码差异是当前asc-comm尚未包含AIN提交`5101ff8`（`perf(hcomm): reduce SIMT WQE GM stores`）：当前路径先清空128B WQE，再逐字段写GM；该优化改为按16个64位完整字直接写入，避免清空和位域GM读改写。该差异位于本样例的计时区间内，后续应作为独立性能优化迁移并复测。

## 绘图

绘图脚本只使用 Python 标准库，不依赖 `matplotlib`：

```bash
python3 examples/simt_notify_atomic_perf/plot.py \
    examples/simt_notify_atomic_perf/results/notify_*.log \
    --api notify \
    --metric bandwidth \
    --csv examples/simt_notify_atomic_perf/results/notify.csv \
    -o examples/simt_notify_atomic_perf/results/notify_bandwidth.svg
```

横轴为数据大小（B，对数刻度），纵轴为 `IssueBandwidth/GB/s`。当前日志只包含 `SIMT-t1` 曲线。

也可以绘制平均 WQE 时延或包率：

```bash
python3 examples/simt_notify_atomic_perf/plot.py results/*.log --metric latency
python3 examples/simt_notify_atomic_perf/plot.py results/*.log --metric packet-rate
```
