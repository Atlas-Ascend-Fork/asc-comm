# Hcomm SIMT Batch 功能与下发耗时用例

覆盖 WriteNbi、ReadNbi、WriteWithNotifyNbi、WriteValueNbi、AtomicFAA、AtomicCAS，统一承担原 simt_batch 的功能验证和性能测试。

- Immediate：单lane逐请求直接提交。
- ExplicitBatch：单lane执行 Append → BatchCommit，batch size固定为1。
- GroupBatch：多lane各构造一个请求，每批一次BatchCommit；batch size等于lane数。

每项检查设备返回状态及数据；Read检查本地数据，Atomic检查fetch返回值及远端结果，Notify检查数据和通知值。支持2卡或8卡，8卡按0→1、2→3、4→5、6→7配对，每对分别输出结果。

## 构建与执行

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch_perf
bash build.sh

# 覆盖原最小功能用例：单lane Write → BatchCommit → Drain及远端数据校验。
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
constexpr uint32_t bytes = AscendC::simt::HcommSimtBatchBufferBytes(1U);
alignas(128) __ubuf__ uint64_t workspace[bytes / sizeof(uint64_t)];
AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0U) != 0) return;
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), bytes, remote, 1U);
if (hcomm.WriteNbi(batch, remote, local, 64U) != 0) return;
if (hcomm.BatchCommit(batch) != 0) return;
if (hcomm.Drain(batch) != 0) return;
```

BatchHandle绑定remote所在的远端注册区，后续目标地址必须位于同一区域。最后的1U表示普通Write的每项BB数。

## 输出

与multi_jetty_batch_perf使用同一格式，每个rank pair每项一行：

```text
RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
```

主要指标 `IssueTicksPerRequest` = 计时提交区间的设备 `clock()` 差值 / 请求数，越低越好。包含构造、提交及提交路径同步，不包含随后Drain的等待；单位为原始ticks，未换算为ns。只比较PASS行，不与未对齐时钟口径的SIMD数据直接算加速比。

BatchSize是每批请求数；Jettys和ConcurrentJettys分别是配置数量和实际最大并行数量；Pair是rank对。单Jetty本例两者均为1。功能检查全部保留，正常仅汇总结果，失败才展开rank日志。默认不输出完成带宽、门铃计数或分项诊断。
