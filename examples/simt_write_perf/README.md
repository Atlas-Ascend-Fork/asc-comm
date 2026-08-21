# simt_write_perf

测量 SIMT 路径上 `WriteNbi` 与 `WriteValueNbi` 的发送性能。结构与 `simt_notify_atomic_perf`
一致：rank 0 发送并计时，rank 1 只持有注册内存并校验落地结果。

## 测什么

两个接口 × 两种发布策略，共四个 kernel：

| API | 说明 |
| --- | --- |
| `write` | `WriteNbi`，WQE 通过 SGE 指向本地缓冲区，payload 长度可变 |
| `write_value` | `WriteValueNbi`，8 字节 payload 内联在 WQE 中，不读本地缓冲区 |

| CommitMode | 说明 |
| --- | --- |
| `immediate` | 每条 WQE 都 `commit=true`，各自敲一次 doorbell |
| `last` | 除最后一条外全部 `commit=false`，由最后一条提交发布整批 |

这两种策略的差异不只在 doorbell 次数上，SQ 占用也不同：延迟提交的 `WriteNbi` 是 1 个
64 字节 basic block，而立即提交的会把 payload 拆成两个 SGE 以填满 128 字节的 DWQE 窗口，
占 2 个 block。所以 `immediate` 每次操作消耗的 SQ 空间是 `last` 的两倍。输出里的
`SqBlocksPerWqe` 记录了这一点。

`WriteValueNbi` 无论哪种模式都是 2 个 block。

## 输出

```
RESULT | Path=SIMT-t1 | API=write | DataSize/B=4096 | WqeCount=1024 | Warmup=100
       | CommitMode=Immediate | SqBlocksPerWqe=2 | Slots=64 | IssueTime/us=...
       | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=...
       | DrainStatus=0 | Completed=1024
```

`IssueTime` 只覆盖发送循环，`CompletionTime` 还包含等待所有 CQE 的 `Drain`，两者分别对应
发送开销与端到端时延。`DrainStatus` 非 0 时 cycle 计数被置 0，避免把失败的运行误读成快。

`CompletionBandwidth` 用 `DataSize × WqeCount / CompletionCycles` 计算。`write_value` 的
`DataSize` 恒为 8，与 `--payload-bytes` 无关。

## 用法

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_write_perf/build.sh

# 单点
examples/simt_write_perf/run.sh 2 write

# 延迟提交
examples/simt_write_perf/run.sh 2 write --commit-mode last

# payload 扫描：8 16 64 256 1024 4096 16384 65536
examples/simt_write_perf/run.sh 2 write --sweep

# 内联写
examples/simt_write_perf/run.sh 2 write_value
```

通信域通过 `HcclGetRootInfo` 和 `HcclCommInitRootInfo` 创建，因此不需要 rank table。`run.sh` 为每轮
运行创建临时 root info 文件，rank 0 写入，其余 rank 读取。

选项：`--iterations`（默认 1024）、`--warmup`（默认 100）、`--payload-bytes`（默认 4096）、
`--commit-mode`（默认 immediate）、`--sweep`、`--log`。

`--sweep` 会为每个 payload 起一轮新进程，并各自使用独立的同步目录和 root info 文件——否则上一轮的
barrier 标记文件会让下一轮直接通过，上一轮的 root info 也会被下一轮误读。

## 约束

**`--payload-bytes` 最小为 2。** 立即提交的 `WriteNbi` 把 payload 拆成
`firstLen = len >> 1` 和 `lastLen = len - firstLen` 两段，1 字节会让第一段长度为 0，而 URMA
不接受长度为 0 的 SGE。

**单线程。** 一个 channel 只能由一个 lane 驱动，kernel 以 `dim3(1)` 启动。这是接口约定：
doorbell 发布的 head 覆盖其下所有 slot，多 lane 并发会发布邻居尚未写完的 WQE。

**连续 WQE 轮转写入 64 个 slot**，每个 slot 按 128 字节对齐，避免所有操作命中同一 cache
line 而把远端内存子系统的行为混进发送开销里。slot 数受 64 MiB 缓冲区预算约束，payload
过大时会自动减少。

## 校验

rank 1 逐 slot 检查内容：`write` 检查每个字节是否为 `0x5A`，`write_value` 检查是否为
`0x1122334455667788`。slot 在多次迭代间被重复写入，所以按 slot 而非按操作校验——最后一次
写入留下的字节与第一次相同。接收缓冲区初始化为 0，未落地的 slot 会读回 0。

`write_value` 模式下发送缓冲区被填为 0 而非 `0x5A`，这样即使接收缓冲区意外镜像了发送
缓冲区也不会被误判为内联写成功。
