# Hcomm SIMT WriteNbi 样例

本样例验证 SIMT `WriteNbi` 的单线程提交方式。rank 0 通过一个 lane 向 rank 1 的注册内存写入
32 个 `uint64_t`，rank 1 在 Host 侧校验结果。

## 接口约束

同一 channel 上的 `WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`AtomicFAA`、`AtomicCAS` 和
`Drain` 必须由同一个 lane 串行调用。当前接口不支持多个 lane 并发向同一 channel 写 WQE。

SIMT 暂不对外提供 `WriteValueNbi`。

## 测试模式

| mode | Kernel | 提交方式 |
|---|---|---|
| `single`（默认） | `SimtWriteSingle` | 32 次独立的 `WriteNbi<commit=true>` |
| `batch_last` | `SimtWriteBatchLast` | 31 次串行 `WriteNbi<false>`，最后一次 `WriteNbi<true>` 统一发布 |

两种模式均只使用一个 SIMT lane，最后调用 `Drain` 等待完成。

## 编译

```bash
cd examples/simt_write
mkdir -p build && cd build
cmake .. -DCMAKE_ASC_ARCHITECTURES=dav-3510
make
```

## 运行

```bash
examples/simt_write/run.sh <nranks> <rankTableFilePath> [mode]

examples/simt_write/run.sh 2 /path/to/rank_table.json single
examples/simt_write/run.sh 2 /path/to/rank_table.json batch_last
```

预期输出：

```text
[rank 0] simt_write mode=<mode> sending to rank 1
[rank 1] simt_write mode=<mode> slots=32 PASS
```

样例使用 `HcclCommInitClusterInfo` 创建通信域，因此需要传入 rank table。Host 侧通过 `ready` 和
`sent` 两个文件 barrier 保证通道创建完成后再发送、发送完成后再校验。
