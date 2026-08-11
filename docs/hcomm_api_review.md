# Hcomm 接口文档（批量写接口评审）

- **仓库**: [cann/asc-comm](https://gitcode.com/cann/asc-comm)
- **接口来源**: [include/aicore/hcomm/hcomm.h (master)](https://gitcode.com/cann/asc-comm/blob/master/include/aicore/hcomm/hcomm.h)
- **文档内容**:
  - 第 1 章为本次评审范围与说明；
  - 第 2 章为单 Jetty 单对端批量接口；
  - 第 3 章为单 Jetty 多对端批量接口（算子优选）；
  - 第 4 章为单 Jetty 多对端批量接口备选。

---

## 目录

- [1. 评审范围](#1-评审范围)
- [2. 单 Jetty 单对端批量接口](#2-单-jetty-单对端批量接口)
  - [2.1 MakeBatchHandle](#21-makebatchhandle)
  - [2.2 WriteNbi（批量句柄重载）](#22-writenbi批量句柄重载)
  - [2.3 BatchCommit](#23-batchcommit)
  - [2.4 Drain（批量句柄重载）](#24-drain批量句柄重载)
  - [2.5 使用样例](#25-使用样例)
- [3. 单 Jetty 多对端批量接口（算子优选）](#3-单-jetty-多对端批量接口算子优选)
  - [3.1 方案说明](#31-方案说明)
  - [3.2 MultiChannelHandle 定义](#32-multichannelhandle-定义)
  - [3.3 Host 侧接口](#33-host-侧接口)
  - [3.4 Device 侧接口](#34-device-侧接口)
  - [3.5 使用样例](#35-使用样例)
- [4. 单 Jetty 多对端批量接口备选](#4-单-jetty-多对端批量接口备选)
  - [4.1 背景与目标](#41-背景与目标)
  - [4.2 Host 侧接口](#42-host-侧接口)
  - [4.3 Device 侧接口](#43-device-侧接口)
  - [4.4 使用样例](#44-使用样例)
  - [4.5 待评审问题](#45-待评审问题)

---

## 1. 评审范围

本次文档只保留以下接口：

1. 单 Jetty 单对端批量接口，用于单通道下的 Write 积攒与提交；
2. 单 Jetty 多对端批量接口（算子优选），用 `MultiChannelHandle` 统一 host/device 侧对象，并通过 `GetHandleRef` 获取目标端点引用；
3. 单 Jetty 多对端批量接口备选，用于 host 侧预装配多对端上下文，device 侧按 `remoteIndex` 选择目标并批量下发。

旧的“当前已有接口”“上次评审接口”以及 `URMA` 双边接口章节已移除，不再作为本版文档内容。

---

## 2. 单 Jetty 单对端批量接口

### 2.1 MakeBatchHandle

创建批量句柄：将通信通道、目的/源基地址与调用者提供的 UB 积攒 buffer 绑定到一个批量句柄上，并以返回值形式返回该句柄。

```cpp
template <typename T, typename U>
__aicore__ inline T MakeBatchHandle(
    ChannelHandle channel,
    GM_ADDR remoteBaseAddr,
    GM_ADDR localBaseAddr,
    const LocalTensor<U>& buff,
    uint32_t buffLen);
```

| 模板参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，通常由 `Hcomm` 类模板参数 `commProtocol` 自动推导 |
| `U` | `LocalTensor` 元素类型 |

| 参数 | 方向 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄 |
| `remoteBaseAddr` | 输入 | 数据的远端目的基地址 |
| `localBaseAddr` | 输入 | 数据的本端源基地址 |
| `buff` | 输入 | 用于积攒批量任务的 UB `LocalTensor` |
| `buffLen` | 输入 | UB buffer 长度（字节） |

- **返回值**: 类型为 `T` 的批量句柄，调用侧使用 `auto` 接收。

### 2.2 WriteNbi（批量句柄重载）

将一次 Write 任务积攒到句柄 `h` 绑定的 UB buffer 中，不立即下发。

```cpp
template <typename T, auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteNbi(T h, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

| 模板参数 | 说明 |
| --- | --- |
| `T` | 批量句柄类型，由参数 `h` 自动推导 |
| `config` | URMA WQE 控制配置，仅 URMA 使用 |

| 参数 | 方向 | 说明 |
| --- | --- | --- |
| `h` | 输入 | 批量句柄 |
| `dst` | 输出 | 数据的目的地址 |
| `src` | 输入 | 数据的源地址 |
| `len` | 输入 | 写数据长度（字节） |

- **返回值**: `0` 表示成功，`-1` 表示失败。

### 2.3 BatchCommit

通过 MTE3 搬运将句柄 `h` 上积攒的任务从 UB 搬到 SQ 并提交执行。

```cpp
template <typename T, pipe_t pipe = PIPE_S>
__aicore__ inline int32_t BatchCommit(T h);
```

### 2.4 Drain（批量句柄重载）

等待批量句柄 `h` 上提交的通信任务完成。

```cpp
template <typename T, pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t Drain(T h);
```

### 2.5 使用样例

讨论：

1、超过buff 刷新机制

2、不同opcode的刷新策略问题

3、config 放到 WriteNbi 模板参数上，需要测试该方案性能

```cpp
#include "aicore/hcomm/hcomm.h"

using namespace AscendC;

constexpr uint32_t BATCH_BUFF_LEN = 8 * 1024;
constexpr uint32_t BATCH_TASK_NUM = 1000;
constexpr uint64_t DATA_LEN = 512;

__aicore__ inline void BatchWriteExample(ChannelHandle channel, GM_ADDR remoteBase, GM_ADDR localBase)
{
    Hcomm<COMM_PROTOCOL_UBC_CTP> hcomm;

    TPipe pipe;
    TBuf<TPosition::VECCALC> batchBuf;
    pipe.InitBuffer(batchBuf, BATCH_BUFF_LEN);
    LocalTensor<uint8_t> buff = batchBuf.Get<uint8_t>();

    auto h = hcomm.MakeBatchHandle(channel, remoteBase, localBase, buff, BATCH_BUFF_LEN);

    for (uint32_t i = 0; i < BATCH_TASK_NUM; ++i) {
        hcomm.WriteNbi(h, remoteBase + i * DATA_LEN, localBase + i * DATA_LEN, DATA_LEN);
    }

    hcomm.BatchCommit(h);
    hcomm.Drain(h);
}
```

---

## 3. 单 Jetty 多对端批量接口（算子优选）

### 3.1 方案说明

本方案将 `MultiChannelHandle` 作为 host/device 两侧统一可见的多对端句柄值。host 侧通过 `MakeMultiChannelHandle` 创建 channel、申请并初始化多对端句柄；device 侧 launch 入参直接接收同一个 `MultiChannelHandle`，并通过 `GetHandleRef(h, remoteIndex)` 获取目标端点引用。

与第 4 章备选方案相比，本方案仍使用 `GetHandleRef(h, remoteIndex)` 获取目标端点引用，但 host 侧通过 `MakeMultiChannelHandle` 内部完成 channel 创建、内存申请和句柄填充，host/device 类型更容易保持一致。

### 3.2 MultiChannelHandle 定义

`MultiChannelHandle` 是指向 `ChannelEntity` 存储的 opaque handle，对外按 `uint64_t` 暴露，保证 host/device 两侧 launch 参数类型一致。

```cpp
using MultiChannelHandle = uint64_t;

typedef struct {
    uint32_t version;
    uint32_t magicWord;
    uint32_t size;
    uint32_t reserved;
} CommAbiHeader;

struct RemoteInfo {
    uint64_t addr;
    uint64_t size;
    uint32_t tokenId;
    uint32_t tokenValue;
    uint32_t tpID;
    uint64_t rmtEidL;
    uint64_t rmtEidH;
};

struct ChannelEntity {
    CommAbiHeader abiHeader;
    SqContext sqContextAddr;
    CqContext cqContextAddr;
    uint32_t rankDim;
    RemoteInfo* remoteInfo;
};
```

`MakeMultiChannelHandle` 负责创建 channels、申请 device 内存，并初始化 `ChannelEntity[channelNum]`。用户不需要在外部额外申请 `MultiChannelHandle` 对应内存，也不需要直接调用内部填充接口。

### 3.3 Host 侧接口

#### 3.3.1 MakeMultiChannelHandle

创建单 Jetty 多对端句柄。接口内部根据 `channelDescs` 创建 channels，按 `channelNum` 申请 device 内存并初始化多对端句柄，通过 `multiChannel` 输出创建完成的句柄值，返回值用于异常判断。

```cpp
int32_t MakeMultiChannelHandle(
    HcclComm comm,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

| 参数 | 方向 | 说明 |
| --- | --- | --- |
| `comm` | 输入 | HCCL 通信域 |
| `channelDescs` | 输入 | channel 创建描述数组 |
| `channelNum` | 输入 | channel 数量 |
| `multiChannel` | 输出 | 创建完成的多对端句柄值，接口内部完成 device 内存申请 |

- **返回值**: `0` 表示成功，非 `0` 表示失败。

接口内部实现包括以下步骤，这些接口和中间对象对外不可见：

1. 创建 channels：

```cpp
HcclResult HcclChannelAcquireWithConfig(
    HcclComm comm,
    CommEngine engine,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    ChannelHandle* channels);
```

2. 创建 `MultiChannelHandle` 对象：按 `channelNum` 申请对应的 device 内存空间，并将句柄值写入 `multiChannel`。

3. 填充 `MultiChannelHandle` 数据：逐个 channel 写入对应的 `ChannelEntity`。

```cpp
int32_t FillMultiChannelHandle(MultiChannelHandle multiChannel, ChannelHandle channel);
```

### 3.4 Device 侧接口

#### 3.4.1 MakeBatchHandle

根据输入的多对端通信句柄创建 device 侧批量句柄，并绑定 UB 积攒 buffer。该接口不返回状态码，批量句柄通过返回值返回。

其中，`MultiChannelHandle` 为固定输入类型，模板参数 `T` 表示返回的批量句柄类型，模板参数 `U` 表示 `LocalTensor` 元素类型。

```cpp
template <typename T, typename U>
__aicore__ inline T MakeBatchHandle(
    MultiChannelHandle multiChannel,
    const LocalTensor<U>& buff,
    uint32_t buffLen);
```

| 参数 | 方向 | 说明 |
| --- | --- | --- |
| `multiChannel` | 输入 | 多对端通信句柄 |
| `buff` | 输入 | 用于积攒批量任务的 UB `LocalTensor` |
| `buffLen` | 输入 | UB buffer 长度（字节） |

- **返回值**: 创建完成的批量句柄。

#### 3.4.2 GetHandleRef

从多对端批量句柄中获取指定 `remoteIndex` 对应的轻量 handle ref。

```cpp
template <typename T>
__aicore__ inline typename RankBatchHandleTraits<T>::Type GetHandleRef(
    T h,
    uint32_t remoteIndex);
```

| 参数 | 方向 | 说明 |
| --- | --- | --- |
| `h` | 输入 | 多对端批量句柄 |
| `remoteIndex` | 输入 | 目标端点索引 |

- **返回值**: 指定目标端点对应的轻量 handle ref。

#### 3.4.3 WriteNbi（多对端批量重载）

将一次 Write 任务积攒到 handle ref 对应的批量句柄中。

```cpp
template <typename T, auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t WriteNbi(T h, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

| 模板参数 | 说明 |
| --- | --- |
| `T` | `GetHandleRef` 返回的轻量 handle ref 类型，由参数 `h` 自动推导 |
| `config` | URMA WQE 控制配置，仅 URMA 使用 |

| 参数 | 方向 | 说明 |
| --- | --- | --- |
| `h` | 输入 | `GetHandleRef` 返回的轻量 handle ref |
| `dst` | 输出 | 数据的目的地址 |
| `src` | 输入 | 数据的源地址 |
| `len` | 输入 | 写数据长度（字节） |

- **返回值**: `0` 表示成功，`-1` 表示失败。

#### 3.4.4 BatchCommit 和 Drain

`BatchCommit`、`Drain` 复用第 2 章接口形态。`BatchCommit(h)` 提交当前批量句柄中积攒的所有多对端任务；`Drain(h)` 等待该批量句柄相关任务完成。

### 3.5 使用样例

```cpp
void PrepareMultiChannelHandle(
    HcclComm comm,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    MakeMultiChannelHandle(comm, channelDescs, channelNum, multiChannel);
    // 多MR问题
    // 提供一个通过地址查询位置的接口

    // 不同的mem注册完，tokenId和tokenValue是否可以一样  --遗留问题
}

__aicore__ inline void MultiBatchWriteExampleV2(
    MultiChannelHandle multiChannel, GM_ADDR* dstAddr, GM_ADDR* srcAddr, uint32_t channelNum, uint32_t myRankId)
{
    Hcomm<COMM_PROTOCOL_UBC_CTP> hcomm;

    TPipe pipe;
    TBuf<TPosition::VECCALC> batchBuf;
    pipe.InitBuffer(batchBuf, 8 * 1024);
    LocalTensor<uint8_t> buff = batchBuf.Get<uint8_t>();

    auto h = hcomm.MakeBatchHandle(multiChannel, buff, 8 * 1024);

    for (uint32_t remoteIndex = 0; remoteIndex < channelNum; ++remoteIndex) {
        if (remoteIndex == myRankId) {
            continue;
        }

        auto u = hcomm.GetHandleRef(h, remoteIndex);
        hcomm.WriteNbi(u, dstAddr[remoteIndex], srcAddr[remoteIndex], 512);
    }

    hcomm.BatchCommit(h);
    hcomm.Drain(h);
}
```

---

## 4. 单 Jetty 多对端批量接口备选

### 4.1 背景与目标

单 Jetty 多对端场景下，一个 AI Core 需要在同一批次内向多个 rank 下发 Write 任务。如果每次热路径调用都携带并解析完整 channel，上下文处理开销较高。

本节接口的目标是：

1. host 侧一次性装配多对端上下文；
2. device 侧按 `remoteIndex` 获取轻量 handle 引用；
3. 热路径只保留最小参数；
4. 复用 `BatchCommit` 完成统一提交。

### 4.2 Host 侧接口

host 侧多对端批量 handle 对外按 `uint64_t` 暴露。

```cpp
using BatchHandle = uint64_t;
```

host 侧流程如下：

1. 根据 `rankDim` 和 `remoteBaseAddrNum` 计算需要申请的 host 和 device 内存大小；
2. 分别通过 `aclrtMallocHost` 和 `aclrtMalloc` 申请内存；
3. 调用 `MakeBatchHandle` 在 host 侧内存上创建多对端 handle；
4. 调用 `FillBatchHandle` 将指定 rank 和远端地址索引对应的信息写入多对端 handle；
5. 通过 `aclrtMemcpy` 将 `hostMem` 拷贝到 `deviceMem`。

#### 4.2.1 MakeBatchHandle

在 host 侧申请的内存上创建多对端批量 handle。

```cpp
BatchHandle MakeBatchHandle(void* hostMem, uint32_t rankDim, uint32_t remoteBaseAddrNum);
```

#### 4.2.2 GetBatchHandleSize

查询 host 侧 handle 所需内存大小。

```cpp
uint64_t GetBatchHandleSize(uint32_t rankDim, uint32_t remoteBaseAddrNum);
```

#### 4.2.3 FillBatchHandle

填充 host 侧创建的 `batchHandle`，写入指定 rank 和远端地址索引对应的通道信息与远端地址元数据。

```cpp
int32_t FillBatchHandle(
    BatchHandle batchHandle,
    uint32_t rankId,
    ChannelHandle channel,
    GM_ADDR remoteAddr,
    uint32_t remoteBaseAddrIndex);
```

### 4.3 Device 侧接口

第 2 章的 BatchHandle 是“单个 channel 上积攒并提交多次 Write”；本节的多对端重载是“同一个批量句柄下，面向多个端点下发任务”，核心差异在于目标端点可以随 `remoteIndex` 切换，而不是直接绑定单一 channel。

4.3 中涉及 3 个对象：

1. `hostBatchHandle`：host 侧申请的 handle 表，用于写入每个 rank 的通道信息和远端地址；
2. `deviceBatchHandle`：由 `aclrtMemcpy` 拷贝到 device 侧的同一份 handle 表；
3. `handle ref`：从 `deviceBatchHandle` 中按 `remoteIndex` 获取的轻量引用，用于 `WriteNbi`；
4. `remoteBaseAddrIndex`：同一 rank 下的远端 MR/remote base address 索引，用于 host 侧填表时选择目标远端地址元数据。

其中，`MultiChannelHandle` 表示 device 侧的多对端 handle 表，`RankBatchHandleTraits<T>::Type` 表示由 batch handle 派生出的 handle ref 类型。

#### 4.3.1 MakeBatchHandle

根据 host 拷贝到 device 的 handle 表创建 device 侧多对端批量句柄，并绑定 UB 积攒 buffer。该接口与 2.1 同名，为不同参数形态的重载。

```cpp
template <typename T, typename U, auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline T MakeBatchHandle(
    MultiChannelHandle deviceBatchHandle,
    const LocalTensor<U>& buff,
    uint32_t buffLen);
```

#### 4.3.2 GetHandleRef

从多对端批量句柄中获取指定 `remoteIndex` 对应的轻量 handle ref。

```cpp
template <typename T>
__aicore__ inline typename RankBatchHandleTraits<T>::Type GetHandleRef(
    T h,
    uint32_t remoteIndex);
```

#### 4.3.3 WriteNbi（多对端批量重载）

```cpp
template <typename T>
__aicore__ inline int32_t WriteNbi(T h, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

#### 4.3.4 BatchCommit

复用 2.3 的 `BatchCommit`，将多对端任务一次性提交；这里只定义“提交”的动作，不在接口层约束跨 `remoteIndex` 的执行顺序。

#### 4.3.5 Drain（多对端重载）

等待批量句柄 `h` 上提交的通信任务完成。

```cpp
template <typename T, pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t Drain(T h);
```

### 4.4 使用样例

```cpp
constexpr uint32_t MAX_RANK_DIM = 128;

void PrepareMultiBatchHandle(
    uint32_t myRankId, uint32_t rankDim, ChannelHandle* channel, GM_ADDR* remoteBaseAddr, void** deviceMem)
{
    uint64_t handleSize = GetBatchHandleSize(rankDim, remoteBaseAddrNum); //
    void* hostMem = nullptr;
    aclrtMallocHost(&hostMem, handleSize);

    // 1、支持多MR问题， 没有多MR 直接填充； 若是有多MR，需要实现一个二维数组
    // 2、当前存在write（64）和writeWithNotiy（128）两类sqe，使用两个模板
    auto batchHandle = MakeBatchHandle(hostMem, rankDim, remoteBaseAddrNum);

    for (uint32_t rankId = 0; rankId < rankDim; ++rankId) {
        if (rankId == myRankId) {
            continue;
        }
        FillBatchHandle(batchHandle, rankId, channel[rankId], remoteBaseAddr[rankId], remoteBaseAddrIndex);
    }

    aclrtMalloc(deviceMem, handleSize, ACL_MEM_MALLOC_HUGE_FIRST);
    aclrtMemcpy(*deviceMem, handleSize, hostMem, handleSize, ACL_MEMCPY_HOST_TO_DEVICE);

    aclrtFreeHost(hostMem);
}

__aicore__ inline void MultiBatchWriteExample(
    MultiChannelHandle deviceBatchHandle, GM_ADDR* dstAddr, GM_ADDR* srcAddr, uint32_t rankDim, uint32_t myRankId)
{
    Hcomm<COMM_PROTOCOL_UBC_CTP> hcomm;

    TPipe pipe;
    TBuf<TPosition::VECCALC> batchBuf;
    pipe.InitBuffer(batchBuf, 8 * 1024);
    LocalTensor<uint8_t> buff = batchBuf.Get<uint8_t>();

    auto h = hcomm.MakeBatchHandle(deviceBatchHandle, buff, 8 * 1024);

    for (uint32_t remoteIndex = 0; remoteIndex < rankDim; ++remoteIndex) {
        if (remoteIndex == myRankId) {
            continue;
        }
        auto u = hcomm.GetHandleRef(h, remoteIndex);
        hcomm.WriteNbi(u, dstAddr[remoteIndex], srcAddr[remoteIndex], 512);
    }

    hcomm.BatchCommit(h);
    hcomm.Drain(h);
}
```

### 4.5 待评审问题

1. 多对端 `BatchCommit` 是按 rank 分别搬运到各自 SQ，还是要求 UB 中按 SQ 分组后批量搬运；
2. `Drain(T h)` 与既有 `Drain(ChannelHandle)` 的模板匹配约束如何收口；
3. host handle 是否需要持久化 `cqHead/cqTail`，取决于 device 侧是否需要直接完成 CQ 轮询；
4. host/device handle 是否需要支持动态 rank 更新。
