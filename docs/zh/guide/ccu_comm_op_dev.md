# CCU通信算子开发指南

## 概述

本指南介绍如何以[`<<<>>>`](../programming_model/ccu_programming.md#核函数)直调方式开发CCU通信算子，覆盖Host侧资源准备、任务下发与Kernel侧通信执行的全流程。

本指南以AllGather为例展开。AllGather是集合通信算子，功能是将通信域内所有rank的输入按照rankId从小到大的顺序拼接，再将拼接结果发送到所有rank的输出缓冲区，如[图1](#fig-allgather)所示。

**图1** AllGather语义<a id="fig-allgather"></a>

![AllGather语义](./figures/allgather.png)

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 通信协议 | `COMM_PROTOCOL_UB_CTP` |
| 通信引擎 | `COMM_ENGINE_CCU` |

<!-- npu="950" id1 -->
> [!NOTE]说明
> 本样例支持Ascend 950PR/Ascend 950DT系列产品，要求CANN版本不低于9.1.0；单卡环境仅支持编译验证，实际运行至少需要2张NPU。
<!-- end id1 -->

## 总体流程

以[allgather](../../../examples/ccu/ccu_direct/01_allgather/README.md)样例为例，CCU AllGather算子的完整实现流程分为Host侧资源准备与Kernel侧通信执行两个阶段，通信完成后销毁资源。

**Host侧资源准备**（控制面）：在Host侧完成通信任务执行所需的资源准备、任务下发与资源回收。

1. 初始化运行环境：初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 分配内存与初始化输入数据：分配Device内存，初始化输入数据并拷贝到Device。
3. 申请CCU资源：基于HCCL通信域申请CCU Channel和CCU实例资源。
4. 获取内存Token与准备任务参数：通过`asccomm_ccu_get_mem_token`获取输入内存Token，并准备CCU任务参数。
5. 下发CCU通信任务：通过`CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`直调CCU Kernel。
6. 销毁资源：销毁通信域、Stream和Device内存。

**Kernel侧通信执行**（数据面）：任务下发到Stream后，CCU硬件在Kernel侧按前同步、算子执行、后同步的通信编程范式完成数据搬运与同步。

1. Kernel内资源准备：加载任务参数、绑定远端Variable。
2. 前同步：将本端通信内存信息（output地址、token）写入对端并附带同步信号，等待对端就绪后再执行数据搬运。
3. 算子执行：发起跨rank数据搬运与本rank本地拷贝，并等待全部搬运完成。
4. 后同步：通知对端搬运完成，并等待所有对端完成。

Host侧与Kernel侧的关联体现在：Host侧完成资源准备后，通过第5步“下发CCU通信任务”直调Kernel，将Channel句柄、任务参数（taskArgs、kernelArg）随任务下发到Stream；Kernel侧随后通过`ccu::load_arg`加载任务参数、`ccu::GetResByChannel`绑定Variable，再依次执行前同步、算子执行、后同步。其中前同步属于资源准备在数据面的延伸：Host侧准备的Token等通信内存信息，需经前同步交换到对端后，跨rank搬运才能发起。

本章后续内容按此流程展开，代码关键片段均取自[allgather](../../../examples/ccu/ccu_direct/01_allgather/README.md)样例。

## Host侧资源准备

### 初始化运行环境

- 初始化ACL并查询设备数量。

    ```cpp
    // 设备资源初始化
    ACLCHECK(aclInit(NULL));
    // 查询设备数量
    uint32_t devCount;
    ACLCHECK(aclrtGetDeviceCount(&devCount));
    ```

- 生成通信域初始化信息rootInfo。

    ```cpp
    int32_t rootRank = 0;
    ACLCHECK(aclrtSetDevice(rootRank));
    // 生成 Root 节点信息，各线程使用同一份 RootInfo
    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    HcclRootInfo* rootInfo = (HcclRootInfo*)rootInfoBuf;
    HCCLCHECK(HcclGetRootInfo(rootInfo));
    ```

- 各rank初始化通信域并创建Stream。

    每个rank初始化通信域前，先通过`aclrtSetDevice`切换到本rank的设备，再基于同一份rootInfo初始化本rank的通信域，并创建任务Stream。

    ```cpp
    // 设置当前rank操作的设备，device为当前rank对应的设备号（0~devCount-1）
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    // 初始化集合通信域
    HcclComm comm;
    HCCLCHECK(HcclCommInitRootInfo(devCount, rootInfo, device, &comm));

    // 创建任务流
    aclrtStream stream;
    ACLCHECK(aclrtCreateStream(&stream));
    ```

### 分配内存与初始化输入数据

- 申请集合通信所需的Device内存存放输入、输出数据。

    ```cpp
    ACLCHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ```

- 申请Host内存初始化输入数据后拷贝到Device侧。

    ```cpp
    // 申请 Host 内存存放输入数据，并初始化为 Device ID + 1
    void* hostBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&hostBuf, sendSize));

    // 将输入数据从 Host 拷贝到 Device
    ACLCHECK(aclrtMemcpy(sendBuf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtFreeHost(hostBuf));
    ```

### 申请CCU资源

- 查询rankId和rankSize

    ```cpp
    uint32_t rankId = 0;
    uint32_t rankSize = 0;
    HCCLCHECK(HcclGetRankId(comm, &rankId));
    HCCLCHECK(HcclGetRankSize(comm, &rankSize));
    ```

- 为每个对端rank申请Channel

    `kernelChannels`为保存Channel句柄的容器，对本通信域内除本rank外的每一个对端rank，查询链路信息、选取UBC_CTP协议链路填充Channel描述符并申请Channel。

    ```cpp
    kernelChannels.resize(rankSize - 1);
    uint32_t channelIndex = 0;
    for (uint32_t remoteRank = 0; remoteRank < rankSize; remoteRank++) {
        if (remoteRank == rankId) {
            continue;
        }
        uint32_t netLayer = 0;
        uint32_t listSize = 0;
        CommLink* linkList = nullptr;
        // 获取srcRank和dstRank间link信息
        HCCLCHECK(HcclRankGraphGetLinks(comm, netLayer, rankId, remoteRank, &linkList, &listSize));

        HcclChannelDesc desc;
        HCCLCHECK(HcclChannelDescInit(&desc, 1));
        CommProtocol protocol = CommProtocol::COMM_PROTOCOL_UBC_CTP;
        bool protocolExists = false;
        for (uint32_t idx = 0; idx < listSize; idx++) {
            CommLink link = linkList[idx];
            if (link.linkAttr.linkProtocol == protocol) {
                desc.remoteRank = remoteRank;
                desc.notifyNum = CHANNEL_NOTIFY_NUM; // Channel上协商的notify槽位数量
                desc.channelProtocol = link.linkAttr.linkProtocol;
                desc.localEndpoint.protocol = link.srcEndpointDesc.protocol;
                desc.localEndpoint.commAddr = link.srcEndpointDesc.commAddr;
                desc.localEndpoint.loc = link.srcEndpointDesc.loc;
                desc.remoteEndpoint.protocol = link.dstEndpointDesc.protocol;
                desc.remoteEndpoint.commAddr = link.dstEndpointDesc.commAddr;
                desc.remoteEndpoint.loc = link.dstEndpointDesc.loc;
                protocolExists = true;
                break;
            }
        }
        if (!protocolExists) {
            return HCCL_E_NOT_FOUND;
        }
        // 在CCU引擎上申请Channel
        HCCLCHECK(HcclChannelAcquire(comm, CommEngine::COMM_ENGINE_CCU, &desc, 1, &kernelChannels[channelIndex]));
        channelIndex++;
    }
    ```

- 查询Die编号

    首次建链时可查询本端端点所在的Die编号，作为后续调度配置的依据。

    ```cpp
    EndpointAttrDieId dieIdValue = 0;
    HCCLCHECK(HcclRankGraphGetEndpointInfo(
        comm, rankId, &desc.localEndpoint, ENDPOINT_ATTR_DIE_ID, sizeof(EndpointAttrDieId), &dieIdValue));
    ```

- 获取CCU实例：Kernel翻译产生的指令空间、寄存器、片上缓存等资源都挂载在CCU实例上。

    样例通过`HcclCommQueryCcuIns`查询通信域上绑定的CCU实例，公共辅助函数`QueryCcuInstance`封装了该调用，并校验通信域恰好绑定一个实例：

    ```cpp
    CcuInsHandle insHandle{};
    HCCLCHECK(asccomm_examples::QueryCcuInstance(comm, insHandle));
    ```

### 获取内存Token与准备任务参数

- 获取内存Token

    CCU硬件不直接使用进程虚拟地址访问片上内存，须先通过`asccomm_ccu_get_mem_token`将`(虚拟地址, 大小)`转换为驱动颁发的64位Token，再随任务参数传入Kernel。

    ```cpp
    // inputSize为每个rank的输入字节数
    uint64_t token = 0;
    uint64_t baseInputAddr = reinterpret_cast<uint64_t>(sendBuf);
    uint64_t baseOutputAddr = reinterpret_cast<uint64_t>(recvBuf);
    HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_get_mem_token(baseInputAddr, inputSize, &token)));
    ```

    `asccomm_ccu_*`接口返回`CcuResult`，样例中通过`ConvertCcuToHccl`转换为`HcclResult`后交由`HCCLCHECK`检查。

- 准备taskArgs

    `taskArgs`为执行期参数数组，与Kernel函数的SQE参数一一对应。AllGather场景包含输入输出地址、token、卡间偏移、分片大小及本地拷贝的切分参数：

    ```cpp
    uint64_t taskArgs[CCU_DIRECT_TASK_ARG_NUM] = {  // CCU_DIRECT_TASK_ARG_NUM为参数个数，样例为10
        baseInputAddr, baseOutputAddr, token,     0,         inputSize * rankId,
        inputSize,     goSize[0],      goSize[1], goSize[2], goSize[3],
    }; // goSize为CalGoSize计算的本地拷贝切分参数：偏移、循环参数、并行参数、残余大小
    ```

- 准备kernelArg

    `kernelArg`为编排上下文，持有Channel句柄、rankSize、rankId等：

    ```cpp
    auto kernelArg = std::make_shared<CcuKernelArgAllGatherMesh1DMem2Mem>();
    kernelArg->rankSize = rankSize;
    kernelArg->rankId = rankId;
    kernelArg->channelCount = static_cast<uint32_t>(kernelChannels.size());
    for (uint32_t i = 0; i < kernelChannels.size(); ++i) {
        kernelArg->channels[i] = kernelChannels[i];
    }
    ```

### 下发CCU通信任务

- 通过`<<<>>>`直调CCU Kernel，将任务下发到Stream执行。

    ```cpp
    const uint64_t dieMask = 1ULL << dieId;

    asccomm_ccu_schd schd = {1, 0, dieMask, 0};
    CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>(
        taskArgs[0], taskArgs[1], taskArgs[2], taskArgs[3], taskArgs[4], taskArgs[5], taskArgs[6], taskArgs[7],
        taskArgs[8], taskArgs[9], kernelArg.get());
    ```

### 销毁资源

- 通信完成后，销毁通信域、释放Device内存、销毁Stream并重置设备：

    ```cpp
    HCCLCHECK(HcclCommDestroy(hcclComm)); // 销毁通信域
    ACLCHECK(aclrtFree(sendBuf));         // 释放 Device 侧内存
    ACLCHECK(aclrtFree(recvBuf));
    ACLCHECK(aclrtDestroyStream(stream)); // 销毁任务流
    ACLCHECK(aclrtResetDevice(device));   // 重置设备
    ```

## Kernel侧通信执行

以AllGather为例说明数据面的任务编排，Kernel入口如下：

```cpp
__ccu_host__ void CcuAllGatherMesh1DMem2MemKernel(
    uint64_t sqeArgs1, uint64_t sqeArgs2, ..., uint64_t sqeArgs10, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuKernelArgAllGatherMesh1DMem2Mem*>(arg);

    // ctx为Kernel上下文，保存input/output/token及切分参数等编程对象
    AllGatherMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    // 下文前同步、算子执行、后同步三个阶段在该函数内展开
}
```

### Kernel内资源准备

任务编排前，完成编程对象的初始化和任务参数的加载。

- 绑定远端Variable单元

    output地址和token属于需要从对端rank写入的Variable，通过`ccu::GetResByChannel`与Channel绑定，`OUTPUT_VAR_ID`、`TOKEN_VAR_ID`由通信双方约定：

    ```cpp
    // OUTPUT_VAR_ID、TOKEN_VAR_ID分别为对端output地址、token对应的Variable单元编号
    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);
    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            // 通过channel绑定对端rank的output地址与token
            ctx.output[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_VAR_ID);
            ctx.token[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_VAR_ID);
            channelIdx++;
        }
    }
    ```

- 加载任务参数

    `ccu::load_arg`按声明顺序将SQE参数加载到CCU变量中，该指令必须在程序最开始调用：

    ```cpp
    uint32_t argId = 0;
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.input, argId++));                        // 输入地址
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.output[ctx.arg->rankId], argId++));      // 本rank输出地址
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.token[ctx.arg->rankId], argId++));       // 内存token
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.currentRankSliceInputOffset, argId++));  // 卡间输入偏移
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.currentRankSliceOutputOffset, argId++)); // 卡间输出偏移
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.sliceSize, argId++));                    // 分片大小
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.addrOffset, argId++));            // 本地拷贝切分参数
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.loopParam, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.parallelParam, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.residual, argId++));
    ```

- 声明本地拷贝资源

    本地拷贝所需的`ccu_buffer`、`event`等资源，以及搬运源/目的地址，也在此阶段声明和组装（样例中封装在`InitGroupCopyResources`和`ExecuteAllGatherTransfer`内）。

### 前同步

- 前同步保证所有rank都完成资源准备、互换了通信内存信息后再执行数据搬运。使用`ccu::write_variable_with_notify`将本端output地址和token的值写入对端，同时向对端notify单元发送同步信号；随后`ccu::notify_wait`阻塞等待对端rank的同步信号到达：

    ```cpp
    // 将本端output地址与token写入对端，并置位对端EVENT_IDX_0的对应标志位
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_VAR_ID, EVENT_IDX_0, 1 << OUTPUT_VAR_ID));
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.token[ctx.arg->rankId], TOKEN_VAR_ID, EVENT_IDX_0, 1 << TOKEN_VAR_ID));
    }
    // 等待对端将output地址与token写入本端
    uint32_t allBit = (1 << OUTPUT_VAR_ID) | (1 << TOKEN_VAR_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], EVENT_IDX_0, allBit));
    }
    ```

    > [!CAUTION]注意
    > 不可将"写Variable"与"notify_record"拆分为两步调用。硬件不保证两条独立操作的到达顺序，对端收到Notify后读取Variable时存在读到旧值的风险，仅`write_variable_with_notify`能从硬件层保证"先写值后发Notify"的顺序。

    前同步完成后，所有rank都持有了对端output地址和token，可以组装远端地址发起搬运。

### 算子执行

AllGather的执行分为两部分：向其他rank写入本rank数据，以及本rank数据在本地落位。

- 组装搬运地址

    远端目的地址由对端同步来的output地址、token加卡间偏移构成，本地目的地址直接取本端变量：

    ```cpp
    ccu::local_addr src;
    src.addr_ = ctx.input;
    src.addr_ += ctx.currentRankSliceInputOffset;
    src.token = ctx.token[ctx.arg->rankId];

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr_ = ctx.output[rankIdx] + ctx.currentRankSliceOutputOffset;
            localDst.token = ctx.token[rankIdx];
        } else {
            dst[rankIdx].addr_ = ctx.output[rankIdx] + ctx.currentRankSliceOutputOffset;
            dst[rankIdx].token = ctx.token[rankIdx];
        }
    }
    ```

- 提交跨rank写任务

    `ccu::Write`为异步操作，硬件搬运完成时自动置位event中mask对应的标志位：

    ```cpp
    CCU_IF(ctx.sliceSize != 0)
    {
        uint32_t channelId = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask = 1 << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                // 本rank数据写入对端地址
                CCU_CHK_RET(ccu::Write(ctx.arg->channels[channelId], dst[rankIdx], src, ctx.sliceSize, ctx.event, mask));
                channelId++;
            }
        }
    }
    ```

- 本地拷贝

    本rank数据通过`ccu::LocalCopy`在本地内存间搬运。数据量较大时，将搬运切分为多个分片，经CCU Buffer中转，由多个Loop并发执行以撑满带宽（参见[CCU编程](../programming_model/ccu_programming.md)的"并发执行"一节，样例中封装为`GroupCopy`）：

    ```cpp
    // Loop执行体：片上内存 -> CcuBuffer -> 片上内存，两段搬运间用event_wait确认完成
    ccu::LocalCopy(ctx.moRes.ccuBuf[bufBase], loopSrc[index], loopLen[index], loopEvt, 1);
    ccu::event_wait(loopEvt, 1);
    ccu::LocalCopy(loopDst[index], ctx.moRes.ccuBuf[bufBase], loopLen[index], loopEvt, 1);
    ccu::event_wait(loopEvt, 1);
    ```

- 等待搬运完成

    本地与跨rank搬运全部提交后，记录完成事件并等待所有搬运分支完成。Event在硬件搬运完成时自动置位，此处`ccu::event_record`补齐本地拷贝分支的完成标志，`ccu::event_wait`等待全部rank位掩码置位：

    ```cpp
    CCU_CHK_RET(ccu::event_record(ctx.event, 1 << ctx.arg->rankId));
    const uint16_t totalMask = (1 << ctx.arg->rankSize) - 1;
    CCU_CHK_RET(ccu::event_wait(ctx.event, totalMask));
    ```

### 后同步

- 后同步保证所有rank都完成数据接收后，AllGather才算完成。向每个对端发送搬运完成通知，再等待所有对端的完成通知：

    ```cpp
    // 通知对端：本rank已完成数据搬运，POST_SYNC_ID为后同步使用的notify标志位
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_record(ctx.arg->channels[i], EVENT_IDX_0, 1 << POST_SYNC_ID));
    }
    // 等待所有对端完成数据搬运
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], EVENT_IDX_0, 1 << POST_SYNC_ID));
    }
    ```

- 后同步完成后，Kernel编排结束。Host侧通过`aclrtSynchronizeStream`感知任务完成，即可读取`recvBuf`中的AllGather结果。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。
