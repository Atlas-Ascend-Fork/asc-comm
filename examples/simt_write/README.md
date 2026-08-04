# Hcomm SIMT WriteNbi 样例

验证 `hcomm/hcomm_simt.h` 中 `WriteNbi` 的三种提交方式。所有场景都跑在单核上：`single` 和
`batch_last` 使用 1 个 SIMT 线程，`multi_lane` 使用 31 个。

流量是单向的：rank 0 是唯一的发起方，把每一次 `WriteNbi` 都写进 rank 1 注册的内存。rank 1 不
启动 kernel，数据由它的网卡被动落地，校验也由它的 Host 进程完成。第 `i` 个 slot 携带值
`i + 1`，因此缓冲区读出来是 1, 2, ... 32，任何错值都能直接定位到它本该来自哪个 slot。

## 场景

| `mode` | Kernel | 线程数 | 提交方式 |
|---|---|---|---|
| `single`（默认） | `SimtWriteSingle` | 1 | 32 次独立的 `WriteNbi<commit=true>` |
| `batch_last` | `SimtWriteBatchLast` | 1 | 31 次串行 `WriteNbi<false>`，随后 1 次 `WriteNbi<true>` |
| `multi_lane` | `SimtWriteMultiLane` | 31 | 每个 lane 一次 `WriteNbi<false>`，barrier 后由 lane 0 发起 `WriteNbi<true>` |

三种模式都覆盖全部 32 个 slot。`single` 的每次提交都由自己的门铃发布；`batch_last` 依赖最后一次
立即提交携带的 PI 一并覆盖前面 31 条延迟 WQE；`multi_lane` 用的是同一套发布机制，区别在于延迟
WQE 来自不同 lane 通过原子预约争抢 SQ 空间，而不是单 lane 循环。

SIMT 没有 `Commit` 接口，延迟提交的任务只能由后续一次 `WriteNbi<commit=true>` 带出。

`multi_lane` 在延迟提交和最后一次提交之间调用 `asc_syncthreads()`。这是正确性要求，不是调优选
项：最后一次提交的门铃发布的 PI 覆盖整批请求，所以每个 lane 必须先写完自己的 WQE，否则硬件会去
消费仍然是空的 SQ 槽位。

三种模式最后都调用 `Drain`，确保 kernel 返回前 CQ 已被消费。

## 编译

```bash
cd examples/simt_write
mkdir -p build && cd build
cmake .. -DCMAKE_ASC_ARCHITECTURES=dav-3510
make
```

产物为 `examples/simt_write/build/simt_write`。

## 运行

通信域通过 rank table 创建（`HcclCommInitClusterInfo`），因此需要提供 rank table 文件。
rank `i` 运行在 device `i` 上。

```bash
examples/simt_write/run.sh <nranks> <rankTableFilePath> [mode]
```

`mode` 取 `single`、`batch_last` 或 `multi_lane`，默认 `single`。无法识别的取值会直接报错，
而不是静默回退到默认值。

```bash
examples/simt_write/run.sh 2 /path/to/rank_table.json
examples/simt_write/run.sh 2 /path/to/rank_table.json batch_last
examples/simt_write/run.sh 2 /path/to/rank_table.json multi_lane
```

预期输出：

```text
[rank 0] simt_write mode=<mode> sending to rank 1
[rank 1] simt_write mode=<mode> slots=32 PASS
```

rank 0 报告即将发送的内容，rank 1 读自己的接收缓冲区并输出 PASS/FAIL。其余 rank 保持静默。

`run.sh` 捕获 `SIGINT`/`SIGTERM` 并杀掉它启动的 rank 进程，因此 Ctrl+C 不会留下阻塞在 Host
barrier 上、仍占用设备的进程。

## 通道与内存索引

Kernel 侧通过 `AscendC::simt::LocalBufferAddr` 和 `RemoteBufferAddr` 解析缓冲区基址，两者都接受
一个通道缓冲区表的下标。该表由传给 `HcclChannelAcquire` 的 `memHandles` 数组构成，因此本地下标就
是句柄在数组中的位置；远端下标则通过 `HcclChannelGetRemoteMems` 返回的 tag 查找，因为远端表里可
能包含本样例未注册的条目。

## Host 侧同步

两个 Host barrier 保证校验结果可信，它们使用每次运行独立的临时目录下的标记文件，目录路径通过
`SIMT_WRITE_SYNC_DIR` 传给可执行文件。

| Barrier | 作用 |
|---|---|
| `ready` | 所有 rank 都完成内存注册和通道创建，发送方才开始写入 |
| `sent` | 发送方已通过 `aclrtSynchronizeStream`，接收方此时才可以读 |

`sent` barrier 是结果可信的关键。没有它，接收方会从 `ready` 直接跑到 `aclrtMemcpy`，与发送方的
kernel 之间没有任何顺序约束，读到的是被 memset 清零的缓冲区，于是即使 CQE 全部正常返回，也会把
每个 slot 都报成没有落地。
