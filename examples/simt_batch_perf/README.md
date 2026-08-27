# Hcomm SIMT Batch 性能用例

[English](./README_en.md)

该用例逐接口比较单Lane单次提交（`Immediate`）、单Lane批量提交（`ExplicitBatch`）和多Lane协同
批量提交（`GroupBatch`）三条路径，覆盖
`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`WriteValueNbi`、`AtomicFAA` 和 `AtomicCAS`。
三条路径使用相同的数据大小、WQE数量、预热次数及地址轮转方式；两种batch路径每组只调用一次
`BatchCommit`，随后通过`Drain(batch)`等待完成。

每个rank pair最终只输出一条`RESULT`，包含`API`、`DataSize/B`、`Lanes`、`DBCount`、`Mode`、
`Bandwidth/GB/s`、`IssueTime/us`和综合发送端/接收端校验的`Status`。只有失败时才追加
`FAIL_DETAIL`或`CHECK_DETAIL`。

用例通过`HcclGetRootInfo`/`HcclCommInitRootInfo`创建2卡或8卡通信域，不依赖rank table。8卡时
0→1、2→3、4→5和6→7四对并发执行相同配置，每对分别输出性能和校验结果。

## 基线设计

默认一键执行形成每个接口1组Immediate、1组ExplicitBatch以及`2/4/8/16/32 Lane`五组GroupBatch，
共`6个接口 × 7种配置`的42组独立结果。每组重新初始化通信和数据，使用64个地址轮转、96次预热、
1024次计时操作；RMA/Notify默认64B。预热完成后
先 Drain，再开始计时，避免把初始化和预热混入结果。性能数据只有在设备状态、本地 Read/Atomic fetch、
远端目标数据全部通过后才有效。

基础验收先使用默认配置；边界回归使用 `batch-size=1` 和非整批 iterations；优化比较再扫描
`batch-size=2/4/8/16/32`，每组至少重复三次。不同代码版本的结果不能直接合并，日志中的
`CodeVersion` 用于确认二进制来源。

## 基础版与后续优化边界

当前基础版优先复用`simt_urma_after`已验证的单线程路径：六类操作都按其128B immediate WQE
布局写入显式UB；`BatchCommit`逐项预留SQ、复制WQE并写DWQE，先建立六类接口的正确性基线。
它借鉴 SIMD batch PR 的 handle、上下文/token 缓存、WQEBB/CQE 计数和批量提交结构，但不复用
`LocalTensor`、`DataCopy`、`Mutex` 或标量 `st_dev` doorbell 等 AIV 专属机制。

六类接口在Immediate与ExplicitBatch下全部功能通过后，再把多项合并到一次尾WQE发布，并逐项验证以下优化：

1. Write/Read 和 WriteValue 的非尾 WQE 压缩为 1BB，尾 WQE 仍保留已验证的 2BB 布局；
2. 中间 WQE 直接生成到 SQ，减少 UB 到 SQ 的二次搬运；
3. 减少上下文读取、fence 和 CQE 数量；
4. 最后比较 batch size、吞吐和端到端完成时间，确定默认策略。

这些优化不得改变公开接口和单线程 immediate 路径，且每项必须能单独回退。

## 构建

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch_perf
bash build.sh
```

## 执行

不带参数会依次对六个接口运行Immediate、ExplicitBatch和五种Lane数的GroupBatch：

```bash
bash run.sh
```

也可以只测试一个接口：

```bash
bash run.sh write --iterations 4096 --warmup 96 --payload-bytes 64 --batch-size 8
bash run.sh cas --iterations 4096 --batch-size 16
bash run.sh all --devices 8
```

使用`--mode immediate`、`--mode batch`或`--mode group`可只运行一条路径。GroupBatch的Lane数由
`--group-lanes`中的当前值，默认扫描`2,4,8,16,32`；iterations/warmup必须能被每个Lane数整除。
当前每Lane每批准备1项，因此Lane数增加时DB数也会下降，分析时必须同时观察`Lanes`和`DBCount`。
建议至少重复三次，比较稳定的`IssueTime/us`和
`Bandwidth/GB/s`。

输出示例：

```text
TEST_INFO | Suite=simt_batch_perf | CodeVersion=... | ...
RESULT | API=write | DataSize/B=64 | Lanes=8 | DBCount=128 | Mode=GroupBatch | Bandwidth/GB/s=... | IssueTime/us=... | Status=PASS
```
