# Notify WQE 下发性能测试

本样例按 `udma_perftest --metric lat` 和 DesignDocs 第10节的口径，统计单 QP 下“填写 WQE + 敲 DB”的耗时；同时额外记录从开始提交到正式测试 `Drain` 返回的完成耗时和完成带宽。

SIMT 路径为每个 lane 分配独立的 128B WQE 暂存区和 32B 远端注册缓存，并由一个 block 共享只读的 channel/SQ context。日志中的 `RemoteCache=PerLaneUB` 表示远端地址范围与 token 缓存已经启用；缓存命中时可跳过重复的 registered-buffer 查找和 token GM 读取。

当前支持：

- 接口：`Notify`
- `SIMT-t1`：每个 WQE 使用 `commit=true`，每个 WQE 敲一次 DB
- `SIMT-t32` / `SIMT-t1024`：每组由多个 lane 填写 WQE，每个lane使用独立的payload和notify区域；仅最后一个WQE使用 `commit=true`，每组敲一次 DB
- `AIV-MTE3`：AIV 在 UB 中组装 WQE，经 MTE3 写入 SQ，再由 Scalar 敲 DB

所有路径都先预热并 `Drain`，再一次性提交正式 WQE。下发指标在调用正式测试 `Drain` 前结束计时，完成指标则在 `Drain` 返回后结束计时。当前 asc-comm 的 AIV 实现会忽略 `reqPipe`，尚无可独立选择的纯 Scalar WQE 填写路径，因此不输出伪 Scalar 对照数据。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_notify_atomic_perf/build.sh
```

`build.sh` 会先清理本样例的 `build` 目录，再重新配置和编译。

## 快速功能验证

先使用 8B 数据、100 次预热和1次正式提交确认 SIMT-t1：

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

扫描 8B 到 128KiB，每个长度、每条路径正式提交1024个WQE，并预热100个WQE：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
    2 /path/to/rank_table.json notify \
    --loop-count 1024 \
    --warmup 100 \
    --threads 1,32,1024 \
    --paths simt,aiv-mte3 \
    --exponent-range 3 17
```

只测试 4096B：

```bash
bash examples/simt_notify_atomic_perf/run.sh \
    2 /path/to/rank_table.json notify \
    --loop-count 1024 \
    --warmup 100 \
    --threads 1,32,1024 \
    --paths simt,aiv-mte3 \
    --exponent-range 12 12
```

脚本默认将完整输出保存到 `results/notify_<时间>.log`，也可以通过 `--log FILE` 指定路径。只测 SIMT 时可使用 `--simt-only`；`--threads` 目前支持 `1,32,1024` 的任意组合。

## 输出指标

普通运行提示使用中文。为了保持绘图脚本兼容，机器可读的 `RESULT` 行继续使用英文键名：

```text
RESULT | Path=SIMT-t1 | API=notify | DataSize/B=4096 | Npus=2 | WqeCount=1024 | DoorbellCount=1024 | Warmup=100 | TotalTime/us=5375.956000 | AverageWqe/ns=5249.957031 | PacketRate/Mpps=0.190478 | CompletionTime/us=5400.000000 | CompletionAverageWqe/ns=5273.437500 | CompletionBandwidth/GB/s=0.776722 | DrainStatus=0 | Completed=1024
```

- `TotalTime/us`：填写全部 WQE 并敲 DB 的总耗时。
- `AverageWqe/ns = TotalTime / WqeCount`：平均单个 WQE 的下发时延。
- `PacketRate/Mpps = WqeCount / TotalTime(us)`：每秒百万 WQE 数。
- `CompletionTime/us`：从开始填写正式 WQE 到 `Drain` 返回的总耗时。
- `CompletionAverageWqe/ns = CompletionTime / WqeCount`：包含任务完成等待的平均耗时。
- `CompletionBandwidth/GB/s = DataSize(B) / CompletionAverageWqe(ns)`：实际完成口径的有效载荷带宽，可用于和链路带宽上限比较。
- `DoorbellCount`：`SIMT-tN` 为 `ceil(WqeCount/N)`；AIV-MTE3等于`WqeCount`。
- `DrainStatus=0` 且 `Completed=WqeCount`：计时后的统一完成检查成功。

## 生成第10节格式的汇总表

默认汇总4096B数据，并在终端输出Markdown表格：

```bash
python3 examples/simt_notify_atomic_perf/summarize.py \
    examples/simt_notify_atomic_perf/results/notify_*.log
```

汇总全部数据长度并写入文件：

```bash
python3 examples/simt_notify_atomic_perf/summarize.py \
    examples/simt_notify_atomic_perf/results/notify_*.log \
    --all-sizes \
    -o examples/simt_notify_atomic_perf/results/notify_summary.md
```

脚本只统计`DrainStatus=0`且`Completed=WqeCount`的有效结果；失败样本会单独打印，不会混入平均值。传入多个日志时，同一路径、同一数据长度的有效结果取平均值。

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

横轴为数据大小（B，对数刻度），上述命令的纵轴为 `CompletionBandwidth/GB/s`。同一日志中的 `SIMT-t1/t32/t1024` 和 `AIV-MTE3` 会分别生成曲线。

也可以绘制完成时延、下发时延或包率：

```bash
python3 examples/simt_notify_atomic_perf/plot.py results/*.log --metric completion-latency
python3 examples/simt_notify_atomic_perf/plot.py results/*.log --metric latency
python3 examples/simt_notify_atomic_perf/plot.py results/*.log --metric packet-rate
```
