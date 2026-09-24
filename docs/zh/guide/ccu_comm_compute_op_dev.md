# CCU通算融合算子开发指南

## 概述

通算融合算子将AICore上的计算与CCU上的集合通信融合到同一个算子内，本指南基于[add_allgather](../../../examples/ccu/ccu_direct/03_add_allgather/README.md)样例，以Add + AllGather为例，介绍如何以`<<<>>>`直调方式开发通算融合算子。

<!-- npu="950" id1 -->
> [!NOTE]说明
> 本样例支持Ascend 950PR/Ascend 950DT系列产品，要求CANN版本不低于9.1.0；单卡环境仅支持编译验证，实际运行至少需要2张NPU。
<!-- end id1 -->

## 总体流程

通算融合算子的开发除Host侧资源准备与Kernel侧通信执行外，还包含AICore计算Kernel，以及计算与通信之间的同步编排。完整流程如下：

1. 初始化运行环境：初始化ACL与HCCL通信域，创建计算与通信两条Stream。
2. 分配内存与初始化输入数据：额外申请一块`computeBuf`存放计算结果。
3. 建立CCU Channel：基于HCCL通信域为每个对端rank申请CCU Channel。
4. 创建CCU实例并绑定通信域。
5. 申请共享变量与事件：从CCU实例资源池划出AIV与CCU共享的变量与事件。
6. 获取内存Token并准备任务参数。
7. 下发任务：分别通过`vector_add<<<1, nullptr, streamAiv>>>`与`CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>`直调AICore与CCU Kernel。
8. 同步Stream、打印结果并销毁资源。

其中第5、7步是通算融合区别于纯通信算子的关键：AICore计算与CCU通信由两条Stream承载，通过共享变量传递通信地址、通过事件完成双向同步。

## Host侧资源准备

### 初始化运行环境

- 初始化ACL并生成rootInfo。

    ```cpp
    ACLCHECK(aclInit(NULL));
    uint32_t devCount;
    ACLCHECK(aclrtGetDeviceCount(&devCount));

    int32_t rootRank = 0;
    ACLCHECK(aclrtSetDevice(rootRank));
    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    HcclRootInfo* rootInfo = (HcclRootInfo*)rootInfoBuf;
    HCCLCHECK(HcclGetRootInfo(rootInfo));
    ```

- 各rank初始化通信域，并创建计算与通信两条Stream。

    ```cpp
    // 设置当前rank操作的设备
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    // 初始化集合通信域
    HcclComm comm;
    HCCLCHECK(HcclCommInitRootInfo(devCount, rootInfo, device, &comm));

    // 创建计算与通信两条任务流，用于通算并行
    aclrtStream streamAiv;
    ACLCHECK(aclrtCreateStream(&streamAiv));
    aclrtStream streamCcu;
    ACLCHECK(aclrtCreateStream(&streamCcu));
    ```

### 分配内存与初始化输入数据

- 通算融合相比纯通信算子额外申请一块`computeBuf`存放计算结果（即通信输入）。

    ```cpp
    // computeBuf存放计算结果（通信输入），recvBuf存放AllGather结果
    ACLCHECK(aclrtMalloc(&computeBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ```

输入数据初始化与Host->Device拷贝与纯通信算子一致，此处不再展开。

### 建立CCU Channel

与纯通信算子一致，参见[基于CCU的通信算子开发指南·建立CCU Channel](ccu_comm_op_dev.md#建立ccu-channel)，本节不再展开。

### 创建CCU实例并绑定通信域

与纯通信算子一致，参见[基于CCU的通信算子开发指南·创建CCU实例并绑定通信域](ccu_comm_op_dev.md#创建ccu实例并绑定通信域)，本节不再展开。

### 申请共享变量与事件

- AICore与CCU分属不同Stream，无法通过Stream内顺序建立依赖。通算融合通过Host侧从CCU实例资源池划出的共享变量与事件实现跨引擎的数据交互与同步：AICore直接读写GM上的变量、置位事件，CCU Kernel读变量、等待事件。

    ```cpp
    // 申请MAPPED_VARIABLE_NUM个共享变量：分别存放通信输入(computeBuf)、输出(recvBuf)地址
    CcuMappedAddrs mappedAddrs{};
    CcuVariableHandle varHandle{0};
    HCCLCHECK(HcommCcuVariableAlloc(insHandle, ccuDieId, MAPPED_VARIABLE_NUM, &varHandle));
    for (uint32_t i = 0; i < MAPPED_VARIABLE_NUM; i++) {
        HCCLCHECK(HcommCcuVariableGetAddr(varHandle, i, &mappedAddrs.startVa[i]));
    }

    // 申请CKE_NUM个事件：event[0]通知CCU开始通信，event[1]通知AIV通信完成
    CcuEventHandle eventHandle{0};
    HCCLCHECK(HcommCcuEventAlloc(insHandle, ccuDieId, CKE_NUM, &eventHandle));
    for (uint32_t i = 0; i < CKE_NUM; i++) {
        HCCLCHECK(HcommCcuEventGetAddr(eventHandle, i, &mappedAddrs.eventVa[i]));
    }
    ```

变量的VA地址（`mappedAddrs.startVa`）随AICore Kernel参数下发，供其写通信地址；变量、事件的句柄（`varHandle`、`eventHandle`）写入`kernelArg`随CCU Kernel下发，供其读地址、等待/置位事件。

### 获取内存Token与准备任务参数

- `computeBuf`作为通信输入，其内存Token通过`HcommCcuGetMemToken`获取。由于通信输入/输出地址改由共享变量传递，`taskArgs`不再包含地址，只需token与切分参数：

    ```cpp
    // token为computeBuf的内存Token
    uint64_t token = 0;
    HCCLCHECK(HcommCcuGetMemToken(reinterpret_cast<uint64_t>(computeBuf), inputSize, &token));

    uint64_t taskArgs[HCOMM_HOST_TASK_ARG_NUM] = {
        token, 0, inputSize * rankId, inputSize, goSize[0], goSize[1], goSize[2], goSize[3],
    };

    // kernelArg除Channel句柄、rankSize、rankId外，还携带共享变量与事件句柄
    auto kernelArg = std::make_shared<CcuKernelArgAllGatherMesh1DMem2Mem>();
    kernelArg->rankSize = rankSize;
    kernelArg->rankId = rankId;
    kernelArg->channelCount = static_cast<uint32_t>(kernelChannels.size());
    kernelArg->varHandle = varHandle;
    kernelArg->varNum = MAPPED_VARIABLE_NUM;
    kernelArg->eventHandle = eventHandle;
    kernelArg->eventNum = CKE_NUM;
    for (uint32_t i = 0; i < kernelChannels.size(); ++i) {
        kernelArg->channels[i] = kernelChannels[i];
    }
    ```

### 下发任务<<<>>>

- 通过`<<<>>>`分别将CCU Kernel与AICore Kernel下发到通信Stream与计算Stream。CCU Kernel的`<<<>>>`语法与参数、函数签名与纯通信算子一致，参见[基于CCU的通信算子开发指南·核函数](ccu_comm_op_dev.md#核函数)，其SQE参数为`sqeArgs1`~`sqeArgs8`，与taskArgs一一对应（通信地址改由共享变量传递，taskArgs不含地址项）：

    ```cpp
    // <<<>>>直调CCU Kernel下发到通信Stream
    asccomm_ccu_schd schd = {1, 0, dieMask, 0};
    CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>(
        taskArgs[0], taskArgs[1], taskArgs[2], taskArgs[3], taskArgs[4], taskArgs[5], taskArgs[6], taskArgs[7],
        kernelArg.get());

    // <<<>>>直调AICore Kernel下发到计算Stream
    vector_add<float, 256><<<1, nullptr, streamAiv>>>(sendBuf, computeBuf, recvBuf, mappedAddrs);
    ```

### 销毁资源

- 通信完成后，销毁通信域、释放Device内存、销毁Stream并重置设备：

    ```cpp
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtFree(computeBuf));
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    ACLCHECK(aclrtDestroyStream(streamCcu));
    ACLCHECK(aclrtDestroyStream(streamAiv));
    ACLCHECK(aclrtResetDevice(device));
    ```

## Kernel侧通信执行

### AICore计算Kernel

- `vector_add`完成两件事：本地Add计算，以及计算完成后把通信输入、输出地址写入共享变量并置事件通知CCU开始通信；随后轮询事件等待CCU通信完成。

    ```cpp
    template <typename T, uint64_t count>
    __global__ __vector__ void vector_add(
        __gm__ void* srcGm, __gm__ void* computeGm, __gm__ void* dstGm, CcuMappedAddrs mappedAddrs)
    {
        // 1. 本地Add计算：srcGm -> computeGm（与普通AICore kernel一致，此处省略DataCopy/Adds细节）

        // 2. 计算完成后，把通信输入(computeGm)、输出(dstGm)地址写入共享变量
        __gm__ uint64_t* taskArgs0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[0]);
        __gm__ uint64_t* taskArgs1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[1]);
        AscendC::WriteGmBypassDCache<uint64_t>(taskArgs0, reinterpret_cast<uint64_t>(computeGm));
        AscendC::WriteGmBypassDCache<uint64_t>(taskArgs1, reinterpret_cast<uint64_t>(dstGm));
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

        // 3. 置event[0]，通知CCU开始通信
        AscendC::WriteGmBypassDCache<uint64_t>(reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[0]), 1);
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

        // 4. 轮询event[1]，等待CCU通信完成
        while (1) {
            uint64_t waitCke = AscendC::ReadGmBypassDCache(reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[1]));
            AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();
            AscendC::Nop<800>();
            if (waitCke == 1) {
                break;
            }
        }
    }
    ```

其中第4步的`while`轮询为串行等待CCU完成。本样例的Add + AllGather因此为"先算后通"串行执行，未真正重叠通信与计算；若需实现通算并行掩盖，可将该轮询替换为无依赖的计算逻辑（或采用多chunk流水线），用计算掩盖通信时延。

### Kernel内资源准备

- CCU Kernel与纯通信算子的区别在于：通信地址不再通过`LoadArg`注入，而是在执行前从共享变量读取，并以事件与AICore对齐时序。

    ```cpp
    // 等待AICore写入共享变量并置event[0]
    ccu::Array<ccu::Event> events(ctx.arg->eventHandle, ctx.arg->eventNum);
    ccu::EventWait(events[0]);

    // 从共享变量读取通信输入、输出地址
    ccu::Array<ccu::Variable> vars(ctx.arg->varHandle, ctx.arg->varNum);
    ctx.input = vars[0];                   // 通信输入 = computeBuf
    ctx.output[ctx.arg->rankId] = vars[1]; // 通信输出 = recvBuf
    ```

随后绑定远端Variable单元、通过`ccu::LoadArg`加载token与切分参数，与纯通信一致。

### 前同步

与纯通信算子一致，参见[基于CCU的通信算子开发指南·前同步](ccu_comm_op_dev.md#前同步)，本节不再展开。

### 算子执行

与纯通信AllGather一致（向各对端rank写数据完成发送，本地落位并等待数据到达完成接收），参见[基于CCU的通信算子开发指南·算子执行](ccu_comm_op_dev.md#算子执行)，本节不再展开。

### 后同步

与纯通信算子一致，参见[基于CCU的通信算子开发指南·后同步](ccu_comm_op_dev.md#后同步)，本节不再展开。

### 通知计算完成

- AllGather完成后置event[1]，通知AICore退出轮询：

    ```cpp
    ccu::EventRecord(events[1]);
    ```

## 注意事项

- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 计算与通信需分别通过`aclrtSynchronizeStream`同步`streamAiv`与`streamCcu`，二者之间的依赖仅由事件保证，不要用Stream间同步强行串行。
- 共享变量与事件由`HcommCcuVariableAlloc`、`HcommCcuEventAlloc`从CCU实例资源池分配，数量不得超过实例内相应资源份额。
- 一个CCU不能跨Die使用另一个IO Die的网络设备，Kernel内所用Channel须属同一Die。
- 样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。