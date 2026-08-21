# Hcomm SIMT WriteNbi / WriteValueNbi 样例

验证 `hcomm/hcomm_simt.h` 中 `WriteNbi` 和 `WriteValueNbi` 的两种提交方式。所有场景都跑在单核
上，且都由单个 SIMT 线程发起：一个 channel 必须由一个 lane 驱动。

流量是单向的：rank 0 是唯一的发起方，把每一次写操作都送进 rank 1 注册的内存。rank 1 不启动
kernel，数据由它的网卡被动落地，校验也由它的 Host 进程完成。第 `i` 个 slot 携带值 `i + 1`，因此
缓冲区读出来是 1, 2, ... 32，任何错值都能直接定位到它本该来自哪个 slot。

## 场景

`WriteNbi` 从本地缓冲区取数据，WQE 里放的是指向该缓冲区的 SGE：

| `mode` | Kernel | 线程数 | 提交方式 |
|---|---|---|---|
| `single`（默认） | `SimtWriteSingle` | 1 | 32 次独立的 `WriteNbi<commit=true>` |
| `batch_last` | `SimtWriteBatchLast` | 1 | 31 次串行 `WriteNbi<false>`，随后 1 次 `WriteNbi<true>` |

`WriteValueNbi` 把待写的值直接内联在 WQE 中，没有指向本地内存的 SGE，因此这两种模式完全不读发送
缓冲区（该缓冲区仍然注册，只为让两个 rank 的缓冲区表保持对称）：

| `mode` | Kernel | 线程数 | 提交方式 |
|---|---|---|---|
| `value_single` | `SimtWriteValueSingle` | 1 | 32 次独立的 `WriteValueNbi<commit=true>` |
| `value_batch_last` | `SimtWriteValueBatchLast` | 1 | 31 次串行 `WriteValueNbi<false>`，随后 1 次 `WriteValueNbi<true>` |

四种模式都覆盖全部 32 个 slot，接收侧校验逻辑完全相同。`single` 系列的每次提交都由自己的门铃发
布；`batch_last` 系列依赖最后一次立即提交携带的 PI 一并覆盖前面 31 条延迟 WQE。

`WriteValueNbi` 要求 `config` 开启 inline（默认即 `URMA_INLINE_CFG`），且 `sizeof(T)` 不超过 WQE
内联负载区；两者都有编译期检查。

SIMT 没有 `Commit` 接口，延迟提交的任务只能由后续一次 `commit=true` 的提交带出。

一个 channel 上不能承载来自多个 lane 的延迟任务：延迟 WQE 停留在发送队列中，直到后续 `commit=true`
的提交发布覆盖整批的 PI，而该 PI 无法区分是哪个 lane 写入了哪个 basic block。同一批任务必须由同一
个 lane 提交。

所有模式最后都调用 `Drain`，确保 kernel 返回前 CQ 已被消费。

## Workspace

每个参与提交的 lane 都必须先调用 `Init`，由它获得 128 字节的 WQE 暂存区；空间不足时 `Init` 返回
失败，本样例直接放弃提交。样例按 `HcommWorkspaceBytes(laneCount)` 分配：除每 lane 128 字节外，额
外提供每 lane 32 字节和一块 128 字节对齐区域，从而启用每 lane 远端注册缓存和 block 共享提交上下
文（两者可选，缺失时回退到全局内存查表）。

## 编译

```bash
cd examples/simt_write
mkdir -p build && cd build
cmake .. -DCMAKE_ASC_ARCHITECTURES=dav-3510
make
```

产物为 `examples/simt_write/build/simt_write`。

## 运行

通信域通过 `HcclGetRootInfo` 和 `HcclCommInitRootInfo` 创建，因此不需要 rank table。`run.sh` 为每
次运行创建临时 root info 文件，rank 0 写入，其余 rank 读取。rank `i` 运行在 device `i` 上。

```bash
examples/simt_write/run.sh <nranks> [mode]
```

`mode` 取 `single`、`batch_last`、`value_single` 或 `value_batch_last`，默认 `single`。无法识别的
取值会直接报错，而不是静默回退到默认值。

```bash
examples/simt_write/run.sh 2
examples/simt_write/run.sh 2 batch_last
examples/simt_write/run.sh 2 value_single
examples/simt_write/run.sh 2 value_batch_last
```

预期输出：

```text
[rank 0] simt_write mode=<mode> sending to rank 1
[rank 1] simt_write mode=<mode> slots=32 PASS
RESULT | Mode=<mode> | Status=PASS
```

rank 0 报告即将发送的内容，rank 1 读自己的接收缓冲区并输出 PASS/FAIL，`run.sh` 汇总为 `RESULT` 行。
其余 rank 保持静默。

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
