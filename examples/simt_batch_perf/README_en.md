# Hcomm SIMT Batch validation and issue timing

Validates WriteNbi, ReadNbi, WriteWithNotifyNbi, WriteValueNbi, AtomicFAA and AtomicCAS. This example also replaces the minimal simt_batch example.

Immediate posts individual requests from one lane. ExplicitBatch uses one lane and batch size 1. GroupBatch uses one request per lane and one BatchCommit per group; batch size equals lane count. Device status, remote data, local Read data, atomic fetch results and Notify flags are checked as applicable. Two or eight devices are supported; eight devices form four independent rank pairs.

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch_perf
bash build.sh
# The former minimal single-lane Write/BatchCommit/Drain validation.
bash run.sh write --mode batch --batch-size 1
# Default core APIs: Write/Read/Notify, Immediate and GroupBatch.
bash run.sh
# All six APIs. ExplicitBatch is selected separately.
bash run.sh all
bash run.sh cas --mode group --group-lanes 16,32,128
bash run.sh all --mode batch --batch-size 1 --devices 8
```

Defaults: 64 rotating addresses, 96 warmup requests and 1024 timed requests, 64B RMA/Notify payload. Default groups are 2/4/8/16/32/128 lanes. Timed requests must be divisible by group size; warmup is rounded up. Groups above 128 lanes require `bash build.sh --full`.

## Minimal API sequence

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

All batch destinations must belong to the remote registration containing remote. The final 1U specifies the per-item BB count for ordinary Write.

## Output

Both batch performance examples emit one RESULT per rank pair and case:

```text
RESULT | API=write | Mode=GroupBatch | DataSize/B=64 | BatchSize=32 | Jettys=1 | ConcurrentJettys=1 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
```

IssueTicksPerRequest is the device clock() delta over submission divided by the request count. It includes construction, posting and posting synchronization, and excludes the following Drain wait. Lower is better. These are raw ticks, not nanoseconds; only PASS rows are valid. Do not calculate a SIMD speedup until clock units are aligned.

BatchSize is requests per batch. Jettys is the configured count and ConcurrentJettys is the maximum concurrent count; both are 1 here. Pair identifies the ranks. All correctness checks remain; rank logs are printed on failure. Completion bandwidth, doorbell counts and diagnostic breakdowns are omitted from normal output.
