# Hcomm SIMT URMA 功能样例

验证 `hcomm/hcomm_simt.h` 中全部 SIMT URMA 点对点接口：`WriteNbi`、`WriteValueNbi`、
`WriteWithNotifyNbi`、`AtomicFAA` 和 `AtomicCAS`，以及它们的立即提交和延迟提交两种发布方式。

拓扑是环形的：每个 rank 把操作送进 `next = (rank + 1) % nranks` 注册的内存，并校验 `prev =
(rank - 1 + nranks) % nranks` 写进来的结果。因此 3 卡及以上时所有 rank 都参与收发，而不是只有
前两张卡在工作、其余干等。

每个 rank 既是发送方也是接收方，两侧都校验：`sendBuf` 里是本 rank 取回的 fetch 值，`recvBuf` 里
是 prev 写进来的数据。远端写入只落在 `recvBuf`（peer 的下标按 `kRecvBufTag` 查得，`sendBuf` 虽然
也注册在通道上但从不作为远端目标），环上每个 `recvBuf` 恰好只有一个写者，所以两项校验的期望值都
是确定的。

发送内容与 rank 无关，因此接收侧的期望值表不需要按来源 rank 区分。

所有场景都由单个 SIMT 线程发起。一个 channel 必须由一个 lane 驱动：延迟 WQE 停留在发送队列中，
直到后续 `commit=true` 的提交发布覆盖整批的 PI，而该 PI 无法区分是哪个 lane 写入了哪个 basic
block。同一批任务必须由同一个 lane 提交。

## 测试模式

### Write 系列

`WriteNbi` 从本地缓冲区取数据，WQE 里放的是指向该缓冲区的 SGE。第 `i` 个 slot 携带值 `i + 1`，
因此缓冲区读出来是 1, 2, ... 32，任何错值都能直接定位到它本该来自哪个 slot。

| `mode` | Kernel | 提交方式 |
|---|---|---|
| `write_single` | `SimtWriteSingle` | 32 次独立的 `WriteNbi<commit=true>` |
| `write_batch_last` | `SimtWriteBatchLast` | 31 次串行 `WriteNbi<false>`，随后 1 次 `WriteNbi<true>` |

`WriteValueNbi` 把待写的值直接内联在 WQE 中，没有指向本地内存的 SGE，因此这两种模式完全不读发送
缓冲区（该缓冲区仍然注册，只为让两个 rank 的缓冲区表保持对称）：

| `mode` | Kernel | 提交方式 |
|---|---|---|
| `write_value_single` | `SimtWriteValueSingle` | 32 次独立的 `WriteValueNbi<commit=true>` |
| `write_value_batch_last` | `SimtWriteValueBatchLast` | 31 次串行 `WriteValueNbi<false>`，随后 1 次 `WriteValueNbi<true>` |

四种模式都覆盖全部 32 个 slot，接收侧校验逻辑完全相同。`*_single` 的每次提交都由自己的门铃发布；
`*_batch_last` 依赖最后一次立即提交携带的 PI 一并覆盖前面 31 条延迟 WQE。

`WriteValueNbi` 要求 `config` 开启 inline（默认即 `URMA_INLINE_CFG`），且 `sizeof(T)` 不超过 WQE
内联负载区；两者都有编译期检查。

### Notify/Atomic 系列

| `mode` | 说明 |
|---|---|
| `notify` | 提交一次 Notify 操作并调用 `Drain` |
| `faa` | 提交一次 FAA 操作，校验远端累加结果和本地取回的旧值 |
| `cas` | 提交一次 CAS 操作，校验远端交换结果和本地取回的旧值 |
| `single`（默认） | 串行提交 Notify、FAA 和 CAS，每次操作后分别调用 `Drain` |
| `batch_last` | Notify 和 FAA 使用 `commit=false` 延迟提交，最后通过 `commit=true` 的 CAS 提交整个批次 |
| `notify_immediate_repeat` | 单 lane 连续提交 `kNotifyImmediateRepeatCount` 个 `WriteWithNotifyNbi<true>`，每个 WQE 立即敲 DB，最后统一调用一次 `Drain`；用于覆盖连续立即提交场景 |

`kNotifyImmediateRepeatCount` 定义在 [`simt_urma_common.h`](./simt_urma_common.h) 中，当前为 100。

两个系列使用互不重叠的 slot 布局：各自按自身操作需要观测的内容设计，分开之后每种模式的期望值表
都更容易读。缓冲区按两者中较大的布局分配。

SIMT 没有 `Commit` 接口，延迟提交的任务只能由后续一次 `commit=true` 的提交带出。所有模式最后都
调用 `Drain`，确保 kernel 返回前 CQ 已被消费。

## 准备开发验证环境

样例编译时使用已安装到 CANN 目录的 asc-comm 头文件。先在仓库根目录生成并安装开发验证 run 包：

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh --pkg
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --full
```

如果需要指定 CANN 目录，可增加 `--install-path=/path/to/cann`。完整参数参见
[`docs/zh/guide/build_and_test.md`](../../docs/zh/guide/build_and_test.md)。

## 编译

```bash
cd examples/simt_urma
bash build.sh
```

`build.sh` 会先清理旧的 `build` 目录，再重新配置和编译样例，避免复用旧头文件或旧目标文件。

产物为 `examples/simt_urma/build/simt_urma`。

## 运行

通信域通过 `HcclGetRootInfo` 和 `HcclCommInitRootInfo` 创建，因此不需要 rank table。`run.sh` 为每
次运行创建临时 root info 文件，rank 0 写入，其余 rank 读取。rank `i` 运行在 device `i` 上。

```bash
examples/simt_urma/run.sh <nranks> [mode]
```

`nranks` 可以是任意 ≥2 的值：所有 rank 都会在环上收发。

无法识别的 `mode` 会直接报错，而不是静默回退到默认值。

```bash
examples/simt_urma/run.sh 2 write_single
examples/simt_urma/run.sh 2 write_batch_last
examples/simt_urma/run.sh 2 write_value_single
examples/simt_urma/run.sh 2 write_value_batch_last
examples/simt_urma/run.sh 2 notify
examples/simt_urma/run.sh 2 faa
examples/simt_urma/run.sh 2 cas
examples/simt_urma/run.sh 2 single
examples/simt_urma/run.sh 2 batch_last
examples/simt_urma/run.sh 2 notify_immediate_repeat
```

预期输出（以 2 卡为例，每个 rank 各打印一组）：

```text
[rank 0] simt_urma mode=<mode> sending to rank 1
[rank 1] simt_urma mode=<mode> sending to rank 0
[rank 0] simt_urma <mode> PASS | sent to rank 1, received from rank 1
[rank 1] simt_urma <mode> PASS | sent to rank 0, received from rank 0
RESULT | Mode=<mode> | Status=PASS
```

`run.sh` 捕获 `SIGINT`/`SIGTERM` 并杀掉它启动的 rank 进程，因此 Ctrl+C 不会留下阻塞在 Host
barrier 上、仍占用设备的进程。

## 缓冲区下标约束

Kernel 侧通过 `AscendC::simt::LocalBufferAddr` 和 `RemoteBufferAddr` 解析缓冲区基址，两者都接受
一个通道缓冲区表的下标，而不是传给 `HcclChannelAcquire` 的 `memHandles` 数组下标。本地表下标 0 由
CCL buffer 预留，因此本样例中第一个显式注册的 `sendBuf` 使用下标 1；远端 `recvBuf` 下标通过
`HcclChannelGetRemoteMems` 按 tag 查找，因为远端表里可能包含本样例未注册的条目。

直接传 `memHandles` 下标会解析到 CCL buffer：此时 WQE、远端地址和 token 都合法，网卡会正常完成并
返回干净的 CQE，但接收方看到的是 CCL buffer 的内容而不是 payload。

## Host 侧同步

两个 Host barrier 保证校验结果可信，它们使用每次运行独立的临时目录下的标记文件。

| Barrier | 作用 |
|---|---|
| `ready` | 所有 rank 都完成内存注册和通道创建，才开始写入 |
| `sent` | 每个 rank 都已通过 `aclrtSynchronizeStream`，此时才可以读自己的 recvBuf |

`sent` barrier 是结果可信的关键。没有它，一个 rank 会从 `ready` 直接跑到 `aclrtMemcpy`，与写入它
的那个 rank 的 kernel 之间没有任何顺序约束，读到的是被清零的缓冲区，于是即使 CQE 全部正常返回，
也会把每个 slot 都报成没有落地。

目录路径通过命令行参数传入，而不是环境变量：导出的变量容易在 shell wrapper 或调度器上丢掉，一旦
丢掉，所有 rank 会静默回落到同一个默认目录，上一轮遗留的标记文件就可能让 barrier 直接通过。

## 运行约束

- 支持 Ascend 950PR / Ascend 950DT，CANN 软件版本要求 9.1.0 或以上。
- 运行需要同一主机上至少两张 NPU；单卡环境仅支持编译验证。
- 编译依赖 CANN ASC CMake 能力，并在链接阶段依赖 CANN `hcomm` 库。
