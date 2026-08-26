# Hcomm SIMT URMA 性能样例

测量 SIMT 路径上五个点对点接口的发送性能：`WriteNbi`、`WriteValueNbi`、`WriteWithNotifyNbi`、
`AtomicFAA` 和 `AtomicCAS`。rank 0 发送并计时，rank 1 只持有注册内存并校验落地结果。

所有用例都由单个 lane 发起：一个 channel 必须由一个 lane 驱动。

## 只有前两个 rank 参与

这是点对点基准测试：`<nranks>` 可以 ≥2，但只有 rank 0（发送方）和 rank 1（接收方）建链并计时，
其余 rank 打印 `SKIP` 后直接退出，不加入通信域。只保留一个发送方是有意的——多方并发发送会互相
争抢链路和 SQ 资源，测出来的时延和带宽不再具有可比性。

需要多卡收发验证时使用 [`simt_urma`](../simt_urma/README.md)，它是环形拓扑，所有 rank 都参与。

## 测什么

五个接口 × 两种发布策略：

| `api` | 说明 |
| --- | --- |
| `write` | `WriteNbi`，WQE 通过 SGE 指向本地缓冲区，payload 长度可变 |
| `write_value` | `WriteValueNbi`，8 字节 payload 内联在 WQE 中，不读本地缓冲区 |
| `notify` | `WriteWithNotifyNbi`，payload 之后再写一个远端 signal word，payload 长度可变 |
| `faa` | `AtomicFAA`，对同一个远端 8 字节 word 累加 |
| `cas` | `AtomicCAS`，对同一个远端 8 字节 word 比较交换 |

| `commit-mode` | 说明 |
| --- | --- |
| `immediate` | 每条 WQE 都 `commit=true`，各自敲一次 doorbell |
| `last` | 除最后一条外全部 `commit=false`，由最后一条提交发布整批 |

这两种策略的差异不只在 doorbell 次数上，SQ 占用也不同：延迟提交的 `WriteNbi` 是 1 个 basic
block，立即提交的要把 payload 拆成两个 SGE 以填满 128 字节 DWQE 窗口，占 2 个。其余四个接口无论
哪种策略都是固定 2 BB 的 WQE。日志中的 `SqBlocksPerWqe` 给出实际占用。

连续的 WQE 轮转多个 slot，避免都落在同一条 cache line 上——否则测到的是远端内存系统而不是下发路
径。两个原子接口是例外：它们必须打在同一个 word 上，累加才有意义，因此不轮转。

## 计时流程

```text
执行 warmup 次接口
Drain                         不计时

begin = clock()
执行 iterations 次接口
issueEnd = clock()
Drain
completionEnd = clock()
```

`IssueTime` 和 `AverageIssue` 只统计正式 WQE 下发，用于比较接口提交时延；`CompletionTime` 从正式
下发开始统计到 `Drain` 结束，仅用于计算 `CompletionBandwidth`。`DrainStatus` 和 `Completed` 用于
确认本次下发最终正确完成。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_urma_perftest/build.sh
```

## 用法

通信域通过 `HcclGetRootInfo` 和 `HcclCommInitRootInfo` 创建，因此不需要 rank table。`run.sh` 为每
轮运行创建临时 root info 文件，rank 0 写入，其余 rank 读取。

```bash
# 单点
examples/simt_urma_perftest/run.sh 2 write

# 延迟提交
examples/simt_urma_perftest/run.sh 2 write --commit-mode last

# payload 扫描：8 16 64 256 1024 4096 16384 65536
examples/simt_urma_perftest/run.sh 2 write --sweep

# 内联写
examples/simt_urma_perftest/run.sh 2 write_value

# Notify 与原子操作
examples/simt_urma_perftest/run.sh 2 notify --payload-bytes 4096
examples/simt_urma_perftest/run.sh 2 faa
examples/simt_urma_perftest/run.sh 2 cas
```

选项：`--iterations`（默认 1024）、`--warmup`（默认 100）、`--payload-bytes`（默认 4096）、
`--commit-mode`（默认 immediate）、`--sweep`、`--log`。

`--payload-bytes` 只对 `write` 和 `notify` 有效，其余三个接口恒为 8 字节并忽略该选项；对它们使用
`--sweep` 会直接报错，而不是把同一个 8 字节用例重复跑八遍。

`--sweep` 会为每个 payload 起一轮新进程，并各自使用独立的同步目录和 root info 文件——否则上一轮的
barrier 标记文件会让下一轮直接通过，上一轮的 root info 也会被下一轮误读。该目录通过命令行参数传给
可执行文件，而不是环境变量：导出的变量容易在 shell wrapper 或调度器上丢掉，一旦丢掉，所有轮次会
静默共用同一个默认目录。

## 输出

```text
RESULT | Path=SIMT-t1 | API=write | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | SqBlocksPerWqe=2 | Slots=64 | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
[rank 1] simt_urma_perftest write PASS
```

## 约束

**`--payload-bytes` 最小为 2。** 立即提交的 `WriteNbi` 把 payload 拆成 `firstLen = len >> 1` 和
`lastLen = len - firstLen` 两段，1 字节会让第一段长度为 0，而 URMA 不接受长度为 0 的 SGE。

**接收侧只校验 `write` 和 `write_value`。** `notify` 的 signal word 与 payload 共用同一个 slot，
两个原子接口每次迭代都会改写目标 word，落地镜像取决于迭代次数，没有稳定的期望值可比。这两类用例
依赖 `DrainStatus` 和 `Completed` 判断是否正确完成。

## 运行约束

- 支持 Ascend 950PR / Ascend 950DT，CANN 软件版本要求 9.1.0 或以上。
- 运行需要同一主机上至少两张 NPU；单卡环境仅支持编译验证。
- 编译依赖 CANN ASC CMake 能力，并在链接阶段依赖 CANN `hcomm` 库。
