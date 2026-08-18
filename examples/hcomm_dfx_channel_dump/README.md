# HCOMM DFX Channel Dump 验证样例

## 概述

本样例用于验证 HCOMM 异常场景下的 channel dump 流程：

1. Host 侧创建 HCCL 通信域、注册内存并获取真实 `ChannelHandle`。
2. Host 侧注册 runtime exception callback，并通过 `aclrtBinarySetExceptionCallback` 给样例 allgather kernel 绑定 `SampleAllGatherContext`。
3. 默认使用两段式 fault 流程：先运行 `SampleAllGatherCustomKernel` 提交一条 sample allgather WQE，再运行 `SampleAllGatherCallbackFaultKernel` 通过 `trap()` 触发 runtime callback。
4. callback 内只记录异常信息；`aclrtSynchronizeStream` 返回后，在安全点调用 `AscendC::HcommDfxDumpChannelInfo`。
5. `AscendC::HcommDfxDumpChannelInfo` 采集并打印 Channel、Buffer、SqContext/CqContext、WQE/CQE raw 采样信息。

## 目录结构

```text
├── CMakeLists.txt                         // 编译工程文件
├── README.md                              // 样例说明
├── hcomm_dfx_channel_dump_demo.asc        // Host侧建链、callback、dump与打印
├── sample_allgather_def.h                 // Host/Kernel共享上下文
└── sample_allgather_kernel.cpp            // 样例 allgather AIV kernel
```

## 编译

```bash
source ${install_path}/cann/set_env.sh
cd asc-comm/examples/hcomm_dfx_channel_dump
mkdir -p build && cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

DFX queue-read 辅助 kernel 会随 demo 一起编译链接，运行时不需要额外提供 `hcomm_dfx_queue_read_kernel_910_95.o`。

## 用户接入流程

用户在自己的 HCOMM 算子中参考本样例接入维测能力时，建议按下面四步实现：

1. 在 Host 侧保存异常定位所需上下文。
   至少需要保存 `ChannelHandle`、本端/对端通信 buffer 地址、通信长度、rank 信息，并把该上下文通过 `aclrtMalloc`/`aclrtMemcpy` 写到 device 侧，作为异常回调参数。

2. 给目标 kernel 绑定 runtime exception callback 参数。
   使用 `aclrtGetFuncBySymbol -> aclrtFunctionGetBinary -> aclrtBinarySetExceptionCallback`，把用户自定义 callback 和 device 侧上下文地址绑定到实际要诊断的 kernel binary。

3. callback 内只做轻量记录。
   callback 中只记录 task/stream/device 和绑定的上下文地址，不在 callback 内调用 dump 或执行大量 D2H 拷贝；等 `aclrtSynchronizeStream` 返回后再进入安全点处理。

4. 在安全点 dump 并解析 channel 信息。
   根据上下文拿到 `ChannelHandle` 后，调用 `AscendC::HcommDfxGetChannelDumpInfoSize` 申请 buffer，再调用 `AscendC::HcommDfxDumpChannelInfo` 获取 dump 数据并打印 Channel、Buffer、SqContext/CqContext、WQE/CQE raw 信息。

## 运行

默认自动 fork 两个 rank，并触发 callback fault：

```bash
./hcomm_dfx_channel_dump_demo
```

也可以手动启动两个 rank：

```bash
./hcomm_dfx_channel_dump_demo 0 2 tcp://127.0.0.1:29631
./hcomm_dfx_channel_dump_demo 1 2 tcp://127.0.0.1:29631
```

如需观察 dump 内部过程：

```bash
export HCOMM_DFX_DUMP_TRACE=1
```

## 期望输出

关键日志包括：

```text
[hcomm_dfx_demo] CustomCallback entered ...
[hcomm_dfx_demo] exception ctx channel=...
[hcomm_dfx_demo] HcommDfxDumpChannelInfo ret=0 ...
[HcommDfxDump][parsed] Channel[0] ...
[HcommDfxDump][parsed] SqContext[0] ...
[HcommDfxDump][parsed] CqContext[0] ...
[HcommDfxDump][parsed] Hcomm URMA WQE ...
[HcommDfxDump][parsed] Hcomm URMA CQE ...
```

其中 `HcommDfxDumpChannelInfo ret=0` 且能打印 `SqContext/CqContext` 和 WQE/CQE raw，表示 demo 跑通了新接口形式。
