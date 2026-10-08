# Hcomm SIMT Batch 功能与下发耗时用例

覆盖 WriteNbi、ReadNbi、WriteWithNotifyNbi、WriteValueNbi、AtomicFAA、AtomicCAS 的功能验证和性能测试。

- Immediate：单lane逐请求直接提交。
- ExplicitBatch：单lane执行 Append → BatchCommit，batch size固定为1。
- GroupBatch：多lane各构造一个请求，每批一次BatchCommit；batch size等于lane数。

每项检查设备返回状态及数据；Read检查本地数据，Atomic检查fetch返回值及远端结果，Notify检查数据和通知值。支持2卡或8卡，8卡按0→1、2→3、4→5、6→7配对，每对分别输出结果。

## 构建与执行

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/aicore/hcomm/06_simt_batch_perf
bash build.sh

# 单lane Write → BatchCommit → Drain及远端数据校验。
bash run.sh write --mode batch --batch-size 1
# 单次提交与Group Batch对照；默认core只运行Write/Read/Notify。
bash run.sh
# 六类接口全部执行；默认compare包含Immediate与GroupBatch，不包含ExplicitBatch。
bash run.sh all
# 指定接口、分组和设备数。
bash run.sh cas --mode group --group-lanes 16,32,128
bash run.sh all --mode batch --batch-size 1 --devices 8
```

默认64个地址轮转、96次预热和1024次计时操作，RMA/Notify为64B。GroupBatch默认扫描2/4/8/16/32/128 lanes，计时请求数须整除lane数，预热向完整batch上取整。超过128 lanes需用 `bash build.sh --full` 编译。

## 最小调用流程

```cpp
constexpr uint32_t bytes = 256U; // 128B共享上下文 + 128B发布者DWQE镜像
alignas(128) __ubuf__ uint64_t workspace[bytes / sizeof(uint64_t)];
AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0U) != 0) return;
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remote, local, 64U) != 0) return;
if (hcomm.BatchCommit(batch) != 0) return;
if (hcomm.Drain(batch) != 0) return;
```

BatchHandle绑定remote所在的远端注册区，后续目标地址必须位于同一区域。最后的1U表示普通Write的每项BB数。

Group调用使用同一个不保存Group的Hcomm对象，并在每次批次操作末尾显式传入group：

```cpp
// 以下调用由group所有lane参与；每lane构造自己的一个请求。
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U, group);
if (batch.context == nullptr) return;
if (hcomm.WriteNbi(batch, remoteForLane, localForLane, 64U, group) != 0) return;
if (hcomm.BatchCommit(batch, group) != 0) return;
// 所有posting lane返回后，由一个lane调用Drain；Drain不接收group。
```

同一handle从创建到提交必须使用同一Group（成员、rank及size保持一致）和同一UB工作区。无group重载用于单lane handle；size=1的Group使用单lane分派。Write/Read/Notify/WriteValue/FAA/CAS均在末尾接收group入参，显式config模板参数位于group之前。运行用例时，应安装与本仓库源码匹配的run包并重新编译用例。

## 输出

与 `07_multi_jetty_batch_perf` 使用同一格式，每个rank pair每项一行：

```text
RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
```

主要指标 `IssueTicksPerRequest` = 计时提交区间的设备 `clock()` 差值 / 请求数，越低越好。包含构造、提交及提交路径同步，不包含随后Drain的等待；单位为原始ticks，未换算为ns。只比较PASS行，不与未对齐时钟口径的SIMD数据直接算加速比。

BatchSize是每批请求数；Jettys和ConcurrentJettys分别是配置数量和实际最大并行数量；Pair是rank对。单Jetty本例两者均为1。用例执行全部功能检查，正常仅汇总结果，失败时展开rank日志。默认不输出完成带宽、门铃计数或分项诊断。

## RESULT 字段

| 字段 | 含义 |
|---|---|
| API | write、read、notify等通信操作 |
| Mode | Immediate=单lane直接提交；ExplicitBatch=单lane显式Batch；GroupBatch=多lane协作一个Jetty；MultiJetty=多个Jetty并行用例 |
| DataSize/B | 每个请求的数据字节数，不含Notify标志等控制信息 |
| BatchSize | 每个Jetty每批请求数；GroupBatch/MultiJetty每lane构造一项，因此也等于每Jetty的lane数 |
| Jettys | 该rank对配置的Jetty总数 |
| ConcurrentJettys | 单次kernel最多参与提交的Jetty数；例如J8/BS256为4，需要两个kernel |
| IssueTicksPerRequest | 提交计时区间总ticks除以总请求数，越小越好；原始clock()计数，未换算成ns |
| Pair | 通信rank对，例如0-1；8卡时有四对 |
| Status | 两端执行及该用例的正确性检查是否通过，PASS或FAIL |

IssueTicksPerRequest衡量平均下发成本，不是单个请求的端到端延迟。它包括构造/发布和提交路径同步，不包含后续Drain等待；不计预热，多kernel累加提交区间后除以总请求数，也不计host launch间隙。

成功构建不打印编译过程；正常运行只打印RESULT及已有汇总。编译输出保存在build目录的build-output.log，运行配置记录在汇总日志旁的`.metadata`文件。失败时自动显示编译日志末40行或对应rank日志；无需正常回传这些文件。
