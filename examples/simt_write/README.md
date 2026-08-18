# Hcomm SIMT WriteNbi 样例

本样例验证 SIMT `WriteNbi` 的单 lane 和多 lane 延迟提交方式。rank 0 向 rank 1 的注册内存写入
32 个 `uint64_t`，rank 1 在 Host 侧校验结果。

## 接口约束

同一 channel 允许多个 lane 并发执行 `commit=false`。调用方必须在所有延迟 WQE 填写完成后同步，
再由一个 lane 执行唯一的 `commit=true` 并调用 `Drain`。不支持多个 lane 同时执行 `commit=true`。

SIMT `WriteValueNbi` 当前不支持，调用会触发编译报错。

## 测试模式

| mode | Kernel | 提交方式 |
|---|---|---|
| `single`（默认） | `SimtWriteSingle` | 32 次独立的 `WriteNbi<commit=true>` |
| `batch_last` | `SimtWriteBatchLast` | 31 次串行 `WriteNbi<false>`，最后一次 `WriteNbi<true>` 统一发布 |
| `multi_lane` | `SimtWriteMultiLane` | 31 个 lane 并发 `WriteNbi<false>`，同步后由 lane 0 统一发布 |

所有模式最终均只由一个 lane 执行 `commit=true` 和 `Drain`。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash examples/simt_write/build.sh
```

## 运行

样例通过 `HcclGetRootInfo` 和 `HcclCommInitRootInfo` 创建通信域，无需准备 rank table。`run.sh` 会为每次运行创建临时 root info 文件，由 rank 0 写入、其余 rank 读取。

```bash
examples/simt_write/run.sh <nranks> [mode]

examples/simt_write/run.sh 2 single
examples/simt_write/run.sh 2 batch_last
examples/simt_write/run.sh 2 multi_lane
```

预期输出：

```text
[rank 0] simt_write mode=<mode> sending to rank 1
[rank 1] simt_write mode=<mode> slots=32 PASS
```

Host 侧通过 `ready` 和 `sent` 两个文件 barrier 保证通道创建完成后再发送、发送完成后再校验。
