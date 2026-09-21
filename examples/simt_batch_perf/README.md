# Hcomm SIMT Batch 性能用例

[English](./README_en.md)

该用例逐接口比较单Lane单次提交（`Immediate`）和多Lane协同批量提交（`GroupBatch`），覆盖
`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`WriteValueNbi`、`AtomicFAA` 和 `AtomicCAS`。
两条路径使用相同的数据大小、WQE数量、预热次数及地址轮转方式。Group Batch中batch size等于
Lane数，每Lane准备一项，每批只调用一次`BatchCommit`。`ExplicitBatch`模式只保留batch-size=1，
用于验证单Lane Batch固定开销，不再表示单Lane聚合多项。

每个rank pair最终只输出一条`RESULT`，包含`API`、`DataSize/B`、`Lanes`、`DBCount`、`Mode`、
`Bandwidth/GB/s`、每操作`IssueTime/ns`和综合发送端/接收端校验的`Status`。

用例通过`HcclGetRootInfo`/`HcclCommInitRootInfo`创建2卡或8卡通信域，不依赖rank table。8卡时
0→1、2→3、4→5和6→7四对并发执行相同配置，每对分别输出性能和校验结果。

## 基线设计

默认一键执行形成每个接口1组Immediate，以及`2/4/8/16/32/128 Lane`六组GroupBatch。
每组重新初始化通信和数据，使用64个地址轮转、96次预热、
1024次计时操作；RMA/Notify默认64B。预热完成后
先 Drain，再开始计时，避免把初始化和预热混入结果。性能数据只有在设备状态、本地 Read/Atomic fetch、
远端目标数据全部通过后才有效。

基础验收先使用默认配置；单Lane边界使用`--mode batch --batch-size 1`；Lane扩展通过
`--group-lanes`扫描，每组至少重复三次。

## 当前提交路径

`MakeBatchHandle`在共享UB保存一次稳定的队列地址和远端Jetty信息；Append在每个Lane的私有状态中
生成位置无关的请求字段，不写共享UB。`BatchCommit`由Lane 0一次预留整批SQ范围，各Lane直接写
最终SQ槽位；所有Lane完成后，尾Lane写DWQE发布整批。Write/Read/WriteValue为1BB批次，
Notify/FAA/CAS为2BB批次；同一批不混合不同BB数的接口。

## 构建

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch_perf
bash build.sh
```

## 执行

不带参数会依次运行Immediate和默认Lane数的GroupBatch：

```bash
bash run.sh
```

也可以只测试一个接口：

```bash
bash run.sh write --iterations 4096 --warmup 96 --payload-bytes 64 --group-lanes 2,4,8,16,32
bash run.sh cas --iterations 4096 --mode group --group-lanes 16,32,128
bash run.sh write --mode batch --batch-size 1
bash run.sh all --devices 8
```

使用`--mode immediate`、`--mode batch`或`--mode group`可只运行一条路径。GroupBatch的Lane数由
`--group-lanes`中的当前值，默认扫描`2,4,8,16,32,128`；iterations/warmup必须能被每个Lane数整除。
当前每Lane每批准备1项，因此Lane数增加时DB数也会下降，分析时必须同时观察`Lanes`和`DBCount`。
建议至少重复三次，比较稳定的每操作`IssueTime/ns`和
`Bandwidth/GB/s`。

输出示例：

```text
TEST_INFO | Suite=simt_batch_perf | Devices=2 | ...
RESULT | API=write | DataSize/B=64 | Lanes=32 | DBCount=32 | Mode=GroupBatch | Bandwidth/GB/s=... | IssueTime/ns=... | Status=PASS
```

## Group 入参

`Hcomm<>` 不保存 Group；创建 handle、每次 Batch Append 和 `BatchCommit` 显式传入同一个 `group`。Group 的成员、rank 和 size 在 handle 使用期间必须保持一致。无 Group 重载用于单 lane；`Drain` 和 CompletionSet 接口不变。
