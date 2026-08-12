# Hcomm 接口文档

- **仓库**: [cann/asc-comm](https://gitcode.com/cann/asc-comm)
- **接口来源**: [include/aicore/hcomm/hcomm.h (master)](https://gitcode.com/cann/asc-comm/blob/master/include/aicore/hcomm/hcomm.h)
- **文档内容**:
  - 第 1 章为本次接口范围与说明；
  - 第 2 章为单 Jetty 单对端批量接口；
  - 第 3 章为单 Jetty 多对端批量接口；

---

## 目录

- [1. 接口范围](#1-评审范围)
- [2. 单 Jetty 单对端批量接口](#2-单-jetty-单对端批量接口)
  - [2.1 MakeBatchHandle](#21-makebatchhandle)
  - [2.2 WriteNbi（批量句柄重载）](#22-writenbi批量句柄重载)
  - [2.3 BatchCommit](#23-batchcommit)
  - [2.4 Drain（批量句柄重载）](#24-drain批量句柄重载)
  - [2.5 使用样例](#25-使用样例)
- [3. 单 Jetty 多对端批量接口](#3-单-jetty-多对端批量接口算子优选)
  - [3.1 方案说明](#31-方案说明)
  - [3.2 MultiChannelHandle 定义](#32-multichannelhandle-定义)
  - [3.3 Host 侧接口](#33-host-侧接口)
  - [3.4 Device 侧接口](#34-device-侧接口)
  - [3.5 使用样例](#35-使用样例)

---

## 1. 接口范围

本次文档只保留以下接口：

1. 单 Jetty 单对端批量接口，用于单通道下的 Write 积攒与提交；
2. 单 Jetty 多对端批量接口，用 `MultiChannelHandle` 统一 host/device 侧对象，并通过 `GetHandleRef` 获取目标端点引用；
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

## 3. 单 Jetty 多对端批量接口

### 3.1 方案说明

本方案将 `MultiChannelHandle` 作为 host/device 两侧统一可见的多对端句柄值。host 侧通过 `MakeMultiChannelHandle` 创建 channel、申请并初始化多对端句柄；device 侧 launch 入参直接接收同一个 `MultiChannelHandle`，并通过 `GetHandleRef(h, remoteIndex)` 获取目标端点引用。

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

### 3.6 单 Jetty 多对端端到端用例

本节参考 `hccl_test/main.cpp` 与 `hccl_test/op_kernel.cpp` 的端到端组织方式，给出基于第 3 章推荐接口的单 Jetty 多对端批量写样例。host 侧负责 ACL/HCCL 初始化、对称内存注册、`HcclChannelDesc` 生成和 `MultiChannelHandle` 创建；device 侧在一个 kernel 内按 `remoteIndex` 获取轻量 handle ref，并将多对端写任务积攒后统一提交。

#### 3.6.1 Host 侧 main.cpp 样例

```cpp
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "acl/acl.h"
#include "ain/bootstrap.h"
#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hcomm/hcomm_res.h"
#include "hcomm/hcomm_res_entity_defs.h"

#define ACLCHECK(ret) do { \
    if ((ret) != ACL_SUCCESS) { \
        std::cout << "ACL failed, ret=" << (ret) << std::endl; \
        return ret; \
    } \
} while (0)

#define HCCLCHECK(ret) do { \
    if ((ret) != HCCL_SUCCESS) { \
        std::cout << "HCCL failed, ret=" << (ret) << std::endl; \
        return ret; \
    } \
} while (0)

extern void launch_multi_rank_batch_write(
    uint32_t blockDim,
    void* stream,
    uint64_t multiChannel,
    uint64_t* dstAddr,
    uint64_t* srcAddr,
    uint32_t rankDim,
    uint32_t myRankId,
    uint32_t elementCount);

namespace {
constexpr uint32_t kElementCount = 1024;
constexpr uint64_t kSendOffset = 0;
constexpr uint64_t kRecvOffset = 4096;
constexpr uint64_t kShareBufSize = 1024 * 1024 * 1024;
} // namespace

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << " <rank> <nranks> <endpoint>" << std::endl;
        return 1;
    }

    const uint32_t rank = static_cast<uint32_t>(std::atoi(argv[1]));
    const uint32_t nranks = static_cast<uint32_t>(std::atoi(argv[2]));
    const std::string endpoint = argv[3];

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));

    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    void* localRootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&localRootInfoBuf, sizeof(HcclRootInfo)));
    auto* localRootInfo = static_cast<HcclRootInfo*>(localRootInfoBuf);
    if (rank == 0) {
        HCCLCHECK(HcclGetRootInfo(localRootInfo));
    }

    void* allRootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&allRootInfoBuf, sizeof(HcclRootInfo) * nranks));

    ain::BootstrapOptions options;
    options.rank = rank;
    options.nranks = nranks;
    options.endpoint = endpoint;
    std::unique_ptr<ain::Bootstrap> bootstrap = std::make_unique<ain::Bootstrap>();
    bootstrap->create(options);
    bootstrap->all_gather(localRootInfoBuf, allRootInfoBuf, sizeof(HcclRootInfo));
    bootstrap->barrier();

    auto* allRootInfo = static_cast<HcclRootInfo*>(allRootInfoBuf);
    HcclComm comm = nullptr;
    HCCLCHECK(HcclCommInitRootInfo(nranks, &allRootInfo[0], rank, &comm));

    uint32_t* layers = nullptr;
    uint32_t layerNum = 0;
    HcclRankGraphGetLayers(comm, &layers, &layerNum);
    const uint32_t netLayer = layers[0];

    void* shareBuf = nullptr;
    ACLCHECK(aclrtMalloc(&shareBuf, kShareBufSize, ACL_MEM_MALLOC_HUGE_ONLY));

    std::vector<int32_t> initData((kRecvOffset + kElementCount * sizeof(int32_t)) / sizeof(int32_t), -1);
    for (uint32_t i = 0; i < kElementCount; ++i) {
        initData[(kSendOffset / sizeof(int32_t)) + i] = static_cast<int32_t>(rank * kElementCount + i + 1);
    }
    ACLCHECK(aclrtMemcpy(shareBuf, kShareBufSize, initData.data(), initData.size() * sizeof(int32_t),
        ACL_MEMCPY_HOST_TO_DEVICE));

    CommMem regMem{COMM_MEM_TYPE_DEVICE, shareBuf, kShareBufSize};
    HcclMemHandle memHandle = nullptr;
    HCCLCHECK(HcclCommMemReg(comm, "MultiRankShareBuf", &regMem, &memHandle));

    std::vector<HcclChannelDesc> channelDescs;
    channelDescs.reserve(nranks - 1);
    for (uint32_t remoteIndex = 0; remoteIndex < nranks; ++remoteIndex) {
        if (remoteIndex == rank) {
            continue;
        }

        CommLink* links = nullptr;
        uint32_t linkNum = 0;
        HcclRankGraphGetLinks(comm, netLayer, rank, remoteIndex, &links, &linkNum);

        HcclChannelDesc desc;
        HcclChannelDescInit(&desc, 1);
        desc.remoteRank = remoteIndex;
        desc.localEndpoint.protocol = links[0].srcEndpointDesc.protocol;
        desc.localEndpoint.commAddr = links[0].srcEndpointDesc.commAddr;
        desc.localEndpoint.loc = links[0].srcEndpointDesc.loc;
        desc.remoteEndpoint.protocol = links[0].dstEndpointDesc.protocol;
        desc.remoteEndpoint.commAddr = links[0].dstEndpointDesc.commAddr;
        desc.remoteEndpoint.loc = links[0].dstEndpointDesc.loc;
        desc.channelProtocol = links[0].linkAttr.linkProtocol;
        desc.notifyNum = 3;
        desc.memHandles = &memHandle;
        desc.memHandleNum = 1;
        channelDescs.push_back(desc);
    }

    MultiChannelHandle multiChannel = 0;
    MakeMultiChannelHandle(comm, channelDescs.data(), channelDescs.size(), &multiChannel);

    void* hostDstAddr = nullptr;
    void* hostSrcAddr = nullptr;
    void* deviceDstAddr = nullptr;
    void* deviceSrcAddr = nullptr;
    ACLCHECK(aclrtMallocHost(&hostDstAddr, sizeof(uint64_t) * nranks));
    ACLCHECK(aclrtMallocHost(&hostSrcAddr, sizeof(uint64_t) * nranks));
    ACLCHECK(aclrtMalloc(&deviceDstAddr, sizeof(uint64_t) * nranks, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&deviceSrcAddr, sizeof(uint64_t) * nranks, ACL_MEM_MALLOC_HUGE_ONLY));

    auto* dstAddr = static_cast<uint64_t*>(hostDstAddr);
    auto* srcAddr = static_cast<uint64_t*>(hostSrcAddr);
    for (uint32_t remoteIndex = 0; remoteIndex < nranks; ++remoteIndex) {
        dstAddr[remoteIndex] = reinterpret_cast<uint64_t>(shareBuf) + kRecvOffset;
        srcAddr[remoteIndex] = reinterpret_cast<uint64_t>(shareBuf) + kSendOffset;
    }
    ACLCHECK(aclrtMemcpy(deviceDstAddr, sizeof(uint64_t) * nranks, hostDstAddr,
        sizeof(uint64_t) * nranks, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtMemcpy(deviceSrcAddr, sizeof(uint64_t) * nranks, hostSrcAddr,
        sizeof(uint64_t) * nranks, ACL_MEMCPY_HOST_TO_DEVICE));

    launch_multi_rank_batch_write(1, stream, multiChannel,
        static_cast<uint64_t*>(deviceDstAddr), static_cast<uint64_t*>(deviceSrcAddr),
        nranks, rank, kElementCount);
    ACLCHECK(aclrtSynchronizeStream(stream));
    bootstrap->barrier();

    std::vector<int32_t> result(kElementCount);
    ACLCHECK(aclrtMemcpy(result.data(), result.size() * sizeof(int32_t),
        static_cast<uint8_t*>(shareBuf) + kRecvOffset, result.size() * sizeof(int32_t),
        ACL_MEMCPY_DEVICE_TO_HOST));

    std::cout << "rank " << rank << " recv first value: " << result[0] << std::endl;

    bootstrap->finalize();
    ACLCHECK(aclrtFreeHost(localRootInfoBuf));
    ACLCHECK(aclrtFreeHost(allRootInfoBuf));
    ACLCHECK(aclrtFreeHost(hostDstAddr));
    ACLCHECK(aclrtFreeHost(hostSrcAddr));
    ACLCHECK(aclrtFree(deviceDstAddr));
    ACLCHECK(aclrtFree(deviceSrcAddr));
    ACLCHECK(aclrtFree(shareBuf));
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return 0;
}
```

#### 3.6.2 Device 侧 op_kernel.cpp 样例

```cpp
#include "adv_api/hcomm/hcomm.h"
#include "kernel_operator.h"

namespace {
constexpr uint32_t kBatchBufferBytes = 8 * 1024;
constexpr uint64_t kElementSize = sizeof(int32_t);

using HcommType = AscendC::Hcomm<AscendC::CommProtocol::COMM_PROTOCOL_UBC_CTP>;
} // namespace

extern "C" [[bisheng::core_ratio(0, 1)]] __global__ __aicore__ void multi_rank_batch_write(
    uint64_t multiChannel,
    __gm__ uint64_t* dstAddr,
    __gm__ uint64_t* srcAddr,
    uint32_t rankDim,
    uint32_t myRankId,
    uint32_t elementCount)
{
    HcommType hcomm;

    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECCALC> batchBuf;
    pipe.InitBuffer(batchBuf, kBatchBufferBytes);
    AscendC::LocalTensor<uint8_t> buff = batchBuf.Get<uint8_t>();

    auto h = hcomm.MakeBatchHandle(multiChannel, buff, kBatchBufferBytes);
    for (uint32_t remoteIndex = 0; remoteIndex < rankDim; ++remoteIndex) {
        if (remoteIndex == myRankId) {
            continue;
        }

        auto u = hcomm.GetHandleRef(h, remoteIndex);
        for (uint32_t i = 0; i < elementCount; ++i) {
            uint64_t offset = static_cast<uint64_t>(i) * kElementSize;
            hcomm.WriteNbi(u, dstAddr[remoteIndex] + offset, srcAddr[remoteIndex] + offset, kElementSize);
        }
    }

    hcomm.BatchCommit(h);
    hcomm.Drain(h);
}

void launch_multi_rank_batch_write(
    uint32_t blockDim,
    void* stream,
    uint64_t multiChannel,
    uint64_t* dstAddr,
    uint64_t* srcAddr,
    uint32_t rankDim,
    uint32_t myRankId,
    uint32_t elementCount)
{
    multi_rank_batch_write<<<blockDim, nullptr, stream>>>(
        multiChannel, dstAddr, srcAddr, rankDim, myRankId, elementCount);
}
```

---
