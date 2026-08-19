# Hcomm SIMT WriteNbi 样例

本样例验证 SIMT `WriteNbi` 的延迟提交方式。rank 0 向 rank 1 的注册内存写入
32 个 `uint64_t`，rank 1 在 Host 侧校验结果。

## 接口约束

Hcomm SIMT 接口遵循单生产者语义：同一 channel 的通信任务只能由 group rank 0（线性线程块 rank 0）
下发，并在接口返回前完成组同步。因此延迟 WQE 的填写与最终 `commit=true` 发布均由同一 lane 串行完成，
不再需要多 lane 并发填写与跨 lane 同步。

SIMT `WriteValueNbi` 当前不支持，调用会触发编译报错。

## 测试模式

| mode | Kernel | 提交方式 |
|---|---|---|
| `single`（默认） | `SimtWriteSingle` | 32 次独立的 `WriteNbi<commit=true>` |
| `batch_last` | `SimtWriteBatchLast` | 31 次串行 `WriteNbi<false>`，最后一次 `WriteNbi<true>` 统一发布 |
| `multi_lane` | `SimtWriteMultiLane` | 单 lane 串行 31 次 `WriteNbi<false>`，最后一次 `WriteNbi<true>` 统一发布（与 `batch_last` 等价，保留入口用于覆盖该路径） |

所有模式均由 lane 0 串行下发，最终执行 `commit=true` 和 `Drain`。

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
