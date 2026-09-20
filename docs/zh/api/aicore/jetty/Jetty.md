# Jetty

## 功能说明

头文件为：

```cpp
// SIMD (AIV) 路径
#include "jetty/hcomm_jetty.h"
// SIMT (VF) 路径
#include "jetty/hcomm_jetty_simt.h"
```

`HcommJetty`是AICore侧低层Jetty通信接口，直接向Jetty SQ构造并提交WQE，跳过Hcomm层的任务抽象。SIMD路径面向AIV Kernel，通过`HcommJetty::Write`等接口提交单个WQE并可选等待完成；SIMT路径面向SIMT VF Kernel，支持thread/warp/group三种协作粒度构造WQE。

`HcommPeer`从GM Jetty表解析远端队列与内存信息，缓存到用户分配的UB描述符中。SIMD与SIMT共享同一套80B Jetty元数据与48B远端描述符布局，支持SIMT构造WQE、SIMD调用`RingDoorbell`发布的混合模式。

## 类定义

SIMD路径（`AscendC`命名空间）：

```cpp
class HcommJetty {
public:
    template <int64_t timeoutCycles, bool doCommit = true>
    __aicore__ inline int32_t Write(const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes);
    template <int64_t timeoutCycles, typename T, bool doCommit = true>
    __aicore__ inline int32_t WriteValue(const HcommPeer& peer, GM_ADDR dstAddr, T value);
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t WriteWithNotify(
        const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes,
        GM_ADDR notifyAddr, uint64_t notifyValue);
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t Drain();
    __aicore__ inline void AdvanceSq(uint32_t numWqebbs);
    __aicore__ inline void RingDoorbell();
};
```

SIMT路径（`AscendC::simt::jetty`命名空间）在上述能力之上增加协作组接口：`Write`接受`Coop`协作组参数（thread/warp/group），延迟提交通过`AdvanceSq`与`PublishSq`完成。

## 接口能力

| 接口 | SIMD | SIMT |
| --- | --- | --- |
| `Write` | 支持，单WQE提交。 | 支持，thread/warp/group协作构造，最多12个SGE。 |
| `WriteValue` | 支持，值内联在单个WQEBB中。 | 支持。 |
| `WriteWithNotify` | 支持，完成后向远端写入通知值。 | 支持，单SGE。 |
| `Drain` | 支持，轮询CQ直到全部完成。 | 支持，含协作组重载。 |
| `AdvanceSq` | 支持，推进SQ头并发布到device侧。 | 支持，延迟提交路径使用。 |
| `RingDoorbell` | 支持，敲SQ门铃发布已入队WQE。 | 不适用（SIMT通过`PublishSq`写DWQE窗口发布）。 |
| `PublishSq` | 不适用。 | 支持，写入128B DWQE窗口发布队列。 |

## 使用约束

- 仅支持Ascend 950平台（`dav-3510`）的`COMM_PROTOCOL_UB_CTP`路径。
- 一个Jetty对应一个通信通道；`HcommJetty`与`HcommPeer`使用用户分配的UB空间缓存元数据，单个Jetty约需80B元数据与最大WQE的staging空间。
- SIMT延迟提交路径必须依次调用`Write<..., false>`、`AdvanceSq`和`PublishSq`：`AdvanceSq`只推进SQ头并标记终止WQE，`PublishSq`才写入DWQE窗口使硬件取指。
- 同一channel的并发`Write`必须属于同一batch，由group leader统一提交；所有lane完成`HcommJetty`构造后、任一warp开始`Write`前需要一次group同步。
- 混合模式下，SIMT侧完成WQE写入后SIMD侧才能调用`RingDoorbell`，门铃写无法对另一个核发出的写入排序。

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [Write](./Write.md) | 提交URMA写WQE，SIMT路径支持协作组构造与多SGE。 |
| [WriteValue](./WriteValue.md) | 提交内联立即数写WQE。 |
| [WriteWithNotify](./WriteWithNotify.md) | 提交写WQE并在完成时向远端写入通知值。 |
| [Drain](./Drain.md) | 轮询CQ等待已提交WQE完成。 |
| [AdvanceSq](./AdvanceSq.md) | 推进SQ头，延迟提交路径的组成部分。 |
| [PublishSq](./PublishSq.md) | 写入DWQE窗口发布队列，SIMT延迟提交路径的组成部分。 |
| [RingDoorbell](./RingDoorbell.md) | 敲SQ门铃发布已入队WQE，支持SIMT构造/SIMD发布混合模式。 |
