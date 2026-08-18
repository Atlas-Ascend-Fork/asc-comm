# HCOMM WriteReadNbi DFX 异常维测样例

本样例基于 `hcomm_write_read_nbi` 改造，用来演示用户已有 HCOMM 算子如何接入
`HcommDfxDumpChannelInfo`。业务 kernel 仍然是 `kernel_hcomm_write_read_nbi`，第一次
launch 正常执行 `WriteNbi + ReadNbi + Drain`，第二次 launch 同一个 kernel，通过
`CommContext::triggerFault` 触发 `trap()`，随后在 host 安全点 dump Channel/SQ/CQ/WQE/CQE。

## 用户接入这套框架需要做什么

### 1. 注册

在 host 侧把异常回调和 device context 绑定到目标 kernel：

```cpp
AscendC::HcommDfxBindKernelException(reinterpret_cast<const void *>(kernel_hcomm_write_read_nbi), devContext);
```

用户替换成自己的算子时，只需要把 `kernel_hcomm_write_read_nbi` 换成自己的 kernel symbol，并确保
`devContext` 里能定位到当前 HCOMM channel，或者 host 侧能直接拿到 `ChannelHandle`。

### 2. 回调

callback 已经封装在 DFX 框架内部，用户侧不需要自己实现 callback。框架 callback 只记录异常发生，
不做 dump，不做大量 D2H，也不启动 helper kernel。

本样例为了最小化用户改动，异常后直接使用 host 侧已有的 `hcclMgr_.GetChannel()` 获取
`ChannelHandle`，不再从 `devContext` 里 D2H 解析 context。

### 3. 打印

`aclrtSynchronizeStream` 返回后进入 host 安全点，再调用 DFX dump 接口：

```cpp
ChannelHandle channel = hcclMgr_.GetChannel();
AscendC::HcommDfxDumpAndPrintChannelIfException(channel);
```

当前实现中，`HcommDfxDumpAndPrintChannelIfException` 会在检测到异常后调用
`HcommDfxDumpChannelInfo` 完成采集、解析和打印。日志中看到
`HcommDfxDumpChannelInfo ret=0`，并打印 `Channel`、`SqContext/CqContext`、`WQE/CQE raw`
即表示维测流程跑通。

## 本样例的异常触发方式

本样例不新增第二个 fault kernel，而是连续 launch 两次同一个 `kernel_hcomm_write_read_nbi`：

1. 第一次 `triggerFault=0`，正常执行通信，填充 WQE/CQE。
2. 第二次 `triggerFault=1`，kernel 入口直接 `trap()`，触发 runtime exception callback。

这样可以保留用户原始 kernel 入口形式，同时让 dump 时能稳定看到前一次通信留下的 WQE/CQE。

## 采集路径与 helper kernel 限制

DFX dump 由两条独立路径组成，可以分开判断：

- **D2H 路径**（ChannelEntity / SqContext / CqContext / 活计数）：通过普通 `aclrtMemcpy DEVICE_TO_HOST` 完成，与设备是否处于异常态无关，正常情况下必然成功。
- **helper kernel 路径**（WQE / CQE raw bytes）：queue VA 指向设备侧队列区域，普通 D2H 无法可靠读取，因此由一个 AIV helper kernel（`hcomm_dfx_queue_read_kernel`）在设备侧拷贝后再 D2H 回传。此 kernel 在 `trap()` 触发后用独立 stream launch，**异常态下能否成功取决于 runtime 是否允许新任务入队**，结果不确定。

`HcommDfxQueueEntry::readStatus` 会明确记录每个 WQE/CQE 的采集结果：

- `HCOMM_DFX_READ_OK (0)`：helper kernel 成功，payload 有效，打印结构化字段。
- `HCOMM_DFX_READ_LAUNCH_FAIL (1)`：launch / sync / D2H 失败，payload 全零，打印 `(no payload)`。

因此，"没有 WQE/CQE 输出" 或 "readStatus=1" 不代表 D2H 路径或 dump 框架本身有问题，是 helper kernel 在异常态下受限的正常降级行为。Channel / SqContext / CqContext 信息仍可用于定位问题。

## 编译运行

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build && cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./demo
```

也可以手动启动两个 rank：

```bash
./demo 0 2 tcp://127.0.0.1:29621
./demo 1 2 tcp://127.0.0.1:29621
```

预期关键日志：

```text
[HcommDfxDump] exception callback bound devContext=...
[HcommDfxDump] exception callback entered count=...
[hcomm_write_read_nbi_dfx] fault aclrtSynchronizeStream ret=507035
[HcommDfxDump][parsed] Channel[0] ...
[HcommDfxDump][parsed] SqContext[0] ...
[HcommDfxDump][parsed] CqContext[0] ...
[HcommDfxDump][parsed] Hcomm URMA WQE ...
[HcommDfxDump][parsed] Hcomm URMA CQE ...
[HcommDfxDump] HcommDfxDumpChannelInfo ret=0 ...
```
