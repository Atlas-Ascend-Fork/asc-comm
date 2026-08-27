# Hcomm SIMT Batch 性能用例

[English](./README_en.md)

该用例逐接口比较普通立即提交（`Immediate`）和显式 batch（`ExplicitBatch`）两条路径，覆盖
`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`WriteValueNbi`、`AtomicFAA` 和 `AtomicCAS`。
两条路径使用相同的数据大小、WQE 数量、预热次数及地址轮转方式；显式 batch 路径每组只调用一次
`BatchCommit`，随后通过 `Drain(batch)` 等待完成。

每条`KERNEL_RESULT`均包含明确的`Rank`、`Peer`、`API`和`Mode`，并输出：

- `PublishCount`：实际硬件发布次数；当前基础正确性版本逐项复用已验证的immediate DWQE发布路径；
- `IssueTime/us` 和 `AverageIssue/ns`：任务下发耗时；
- `CompletionTime/us`：包含 Drain 的端到端完成耗时；
- `CompletionBandwidth/GB/s`：按有效数据量计算的完成带宽；
- `StatusCode`、`FailurePhase`、`FailureAction` 和 `FailureOperation`：区分失败发生在第几个操作的
  Immediate、Append、BatchCommit 或 Drain；
- `DeviceStatus` 和 `LocalCheck`：只描述 Kernel/本地返回数据，不冒充远端数据的最终结论。

每对中的奇数rank随后输出远端`CHECK`；所有通信对都成功后，脚本才输出唯一的
`CASE | API=... | Mode=... | Devices=... | Status=PASS`。FAA/CAS同时校验每个slot最后一次fetch的旧值，FAA
远端累计值按实际操作次数计算。任何数据不匹配都会打印首个失败的 slot、字段、Actual 和 Expected。

用例通过`HcclGetRootInfo`/`HcclCommInitRootInfo`创建2卡或8卡通信域，不依赖rank table。8卡时
0→1、2→3、4→5和6→7四对并发执行相同配置，每对分别输出性能和校验结果。

## 基线设计

默认一键执行形成 `6 个接口 × 2 种模式` 的 12 组独立结果。每组重新初始化通信和数据，使用 64 个地址
轮转、100 次预热、1024 次计时操作；RMA/Notify 默认64B，显式batch每8项调用一次`BatchCommit`。预热完成后
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

不带参数会依次对六个接口运行 Immediate 和 ExplicitBatch：

```bash
bash run.sh
```

也可以只测试一个接口：

```bash
bash run.sh write --iterations 4096 --warmup 100 --payload-bytes 64 --batch-size 8
bash run.sh cas --iterations 4096 --batch-size 16
bash run.sh all --devices 8
```

使用 `--mode immediate` 或 `--mode batch` 可只运行一条路径，`--log FILE` 可指定日志路径；默认日志保存到
`results/`。`--devices`支持2或8，默认2；`--batch-size`支持1到32。建议至少重复运行三次，并以稳定结果比较`AverageIssue/ns`和
`CompletionTime/us`。

输出示例：

```text
TEST_INFO | Suite=simt_batch_perf | CodeVersion=... | ...
KERNEL_RESULT | Rank=0 | Peer=1 | API=write | Mode=ExplicitBatch | ... | DeviceStatus=PASS | LocalCheck=PASS
CHECK | Rank=1 | Peer=0 | API=write | Mode=ExplicitBatch | PASS
CASE | API=write | Mode=ExplicitBatch | CodeVersion=... | Devices=8 | Pairs=4 | Status=PASS
```
