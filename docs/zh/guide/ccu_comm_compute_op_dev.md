# CCU通算融合算子开发指南

## 概述

通算融合算子将AI Core上的计算与CCU上的集合通信融合到同一个算子内，本指南基于[add_allgather](../../../examples/ccu/ccu_direct/03_add_allgather/README.md)样例，以Add + AllGather为例，介绍如何以`<<<>>>`直调方式开发通算融合算子。

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

以[add_allgather](../../../examples/ccu/ccu_direct/03_add_allgather/README.md)样例为例，通算融合算子的完整实现流程分为Host侧资源准备与Kernel侧通信执行两个阶段，通信完成后销毁资源。

**Host侧资源准备**（控制面）：在Host侧完成通算融合任务执行所需的资源准备、任务下发与资源回收。

1. 初始化运行环境：初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 分配内存与初始化输入数据：每个Device创建一个rank，并初始化输入Buffer。
3. 申请CCU资源：基于HCCL通信域申请CCU Channel、CCU实例、CCU变量和CCU事件资源。
4. 下发AI Core计算任务：通过`vector_add<<<1, nullptr, streamAiv>>>`直调AI Core vector kernel，生成本地计算结果。
5. 获取内存Token与准备任务参数：通过`asccomm_ccu_get_mem_token`获取`computeBuf`内存Token，并准备CCU任务参数。
6. 下发CCU通信任务：通过`CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>`直调CCU Kernel完成AllGather。
7. 销毁资源：销毁HCCL通信域、Stream和Device侧内存。

**Kernel侧通信执行**（数据面）：任务下发到Stream后，AI Core与CCU分别在计算与通信两条Stream上执行，CCU侧按前同步、算子执行、后同步的通信编程范式完成数据搬运与同步，并通过共享变量与事件完成计算与通信之间的同步编排。

1. AI Core计算Kernel：执行本地Add计算，完成后将通信输入、输出地址写入共享变量并置事件通知CCU开始通信，随后轮询事件等待CCU通信完成。
2. Kernel内资源准备：等待AI Core的事件通知，从共享变量读取通信输入、输出地址，再绑定远端Variable、加载任务参数。
3. 前同步：将本端通信内存信息（output地址、token）写入对端并附带同步信号，等待对端就绪后再执行数据搬运。
4. 算子执行：发起跨rank数据搬运与本rank本地拷贝，并等待全部搬运完成。
5. 后同步：通知对端搬运完成，并等待所有对端完成。
6. 通知计算完成：AllGather完成后置位事件，通知AI Core退出轮询。

Host侧与Kernel侧的关联体现在：Host侧完成资源准备后，通过第4、6步分别将AI Core与CCU Kernel直调下发到计算与通信两条Stream，并将Channel句柄、共享变量与事件句柄、任务参数（taskArgs、kernelArg）随任务下发；Kernel侧AI Core完成计算后将通信地址写入共享变量并置事件，CCU侧通过`ccu::load_arg`加载任务参数、`ccu::GetResByChannel`绑定Variable，再依次执行前同步、算子执行、后同步，完成后置事件通知AI Core退出轮询。其中第3步申请的共享变量与事件，以及第4、6步将计算与通信Kernel分别下发到两条Stream，是通算融合区别于纯通信算子的关键：AI Core计算与CCU通信由两条Stream承载，通过共享变量传递通信地址、通过事件完成双向同步。

本章后续内容按此流程展开，代码关键片段均取自[add_allgather](../../../examples/ccu/ccu_direct/03_add_allgather/README.md)样例。

## Host侧资源准备

### 初始化运行环境

- 初始化ACL、生成rootInfo、各rank初始化通信域与纯通信算子一致，参见[基于CCU的通信算子开发指南·初始化运行环境](ccu_comm_op_dev.md#初始化运行环境)，本节不再展开。

- 创建计算与通信两条Stream：AI Core计算任务与CCU通信任务分别由`streamAiv`与`streamCcu`承载，用于通算并行：

    ```cpp
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

- 输入数据初始化及Host->Device拷贝与纯通信算子一致，此处不再展开。

### 申请CCU资源

- CCU Channel及CCU实例的申请与纯通信算子一致，参见[基于CCU的通信算子开发指南·申请CCU资源](ccu_comm_op_dev.md#申请ccu资源)，本节不再展开。

- 申请共享变量与事件：AI Core与CCU分属不同Stream，无法通过Stream内顺序建立依赖。通算融合通过Host侧从CCU实例资源池划出的共享变量与事件实现跨引擎的数据交互与同步：AI Core直接读写GM上的变量、置位事件，CCU Kernel读变量、等待事件。

    ```cpp
    // 申请MAPPED_VARIABLE_NUM个共享变量：分别存放通信输入(computeBuf)、输出(recvBuf)地址
    CcuMappedAddrs mappedAddrs{};
    ccu_variable_handle varHandle{};
    HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_variable_alloc(insHandle, ccuDieId, MAPPED_VARIABLE_NUM, &varHandle)));
    for (uint32_t i = 0; i < MAPPED_VARIABLE_NUM; i++) {
        HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_variable_get_addr(varHandle, i, &mappedAddrs.startVa[i])));
    }

    // 申请EVENT_NUM个事件：event[0]通知CCU开始通信，event[1]通知AIV通信完成
    ccu_event_handle eventHandle{};
    HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_event_alloc(insHandle, ccuDieId, EVENT_NUM, &eventHandle)));
    for (uint32_t i = 0; i < EVENT_NUM; i++) {
        HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_event_get_addr(eventHandle, i, &mappedAddrs.eventVa[i])));
    }
    ```

    `asccomm_ccu_*`接口返回`CcuResult`，样例中通过`ConvertCcuToHccl`转换为`HcclResult`后交由`HCCLCHECK`检查。

    变量的VA地址（`mappedAddrs.startVa`）随AI Core Kernel参数下发，供其写通信地址；变量、事件的句柄（`varHandle`、`eventHandle`）写入`kernelArg`随CCU Kernel下发，供其读地址、等待/置位事件。

### 下发AI Core计算任务

- 通过`vector_add<<<1, nullptr, streamAiv>>>`直调AI Core vector kernel，将Add计算任务下发到计算Stream，生成本地计算结果：

    ```cpp
    // <<<>>>直调AI Core Kernel下发到计算Stream
    vector_add<float, 256><<<1, nullptr, streamAiv>>>(sendBuf, computeBuf, recvBuf, mappedAddrs);
    ```

### 获取内存Token与准备任务参数

- `computeBuf`作为通信输入，其内存Token通过`asccomm_ccu_get_mem_token`获取。由于通信输入/输出地址改由共享变量传递，`taskArgs`不再包含地址，只需token与切分参数：

    ```cpp
    // token为computeBuf的内存Token
    uint64_t token = 0;
    HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_get_mem_token(reinterpret_cast<uint64_t>(computeBuf), inputSize, &token)));

    uint64_t taskArgs[CCU_DIRECT_TASK_ARG_NUM] = {
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
    kernelArg->eventNum = EVENT_NUM;
    for (uint32_t i = 0; i < kernelChannels.size(); ++i) {
        kernelArg->channels[i] = kernelChannels[i];
    }
    ```

### 下发CCU通信任务

- 通过`CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>`直调CCU Kernel，将AllGather通信任务下发到通信Stream。CCU Kernel的`<<<>>>`语法与参数、函数签名与纯通信算子一致，其SQE参数为`sqeArgs1`~`sqeArgs8`，与taskArgs一一对应（通信地址改由共享变量传递，taskArgs不含地址项）：

    ```cpp
    // <<<>>>直调CCU Kernel下发到通信Stream，tag为Kernel句柄缓存标签
    const std::string launchTag = "CcuAllGatherMesh1DMem2MemKernel_rank_" + std::to_string(rankId);
    uint64_t tag = asccomm_ccu_get_launch_hash_tag(launchTag.c_str());

    asccomm_ccu_schd schd = {1, 0, dieMask, tag};
    CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>(
        taskArgs[0], taskArgs[1], taskArgs[2], taskArgs[3], taskArgs[4], taskArgs[5], taskArgs[6], taskArgs[7],
        kernelArg.get());
    ```

    其中`asccomm_ccu_get_launch_hash_tag`按标签字符串生成`schd`中的句柄缓存标签：非0时，相同标签的重复调用会复用已注册的Kernel句柄，避免重复注册；样例按rankId生成标签，保证各rank标签唯一。

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

### AI Core计算Kernel

- `vector_add`依次完成：本地Add计算、把通信输入/输出地址写入共享变量、置event[0]通知CCU开始通信、轮询event[1]等待CCU通信完成。

    ```cpp
    template <typename T, uint64_t count>
    __global__ __vector__ void vector_add(
        __gm__ void* srcGm, __gm__ void* computeGm, __gm__ void* dstGm, CcuMappedAddrs mappedAddrs)
    {
        // 1. 本地Add计算：srcGm -> computeGm（对标量1.0做Adds）
        AscendC::InitSocState();
        AscendC::GlobalTensor<T> srcGlobal;
        AscendC::GlobalTensor<T> dstGlobal;
        srcGlobal.SetGlobalBuffer((__gm__ T*)srcGm);
        dstGlobal.SetGlobalBuffer((__gm__ T*)computeGm);

        AscendC::LocalMemAllocator<AscendC::Hardware::UB> ubAllocator;
        AscendC::LocalTensor<T> srcLocal = ubAllocator.Alloc<T, count>();
        AscendC::LocalTensor<T> dstLocal = ubAllocator.Alloc<T, count>();

        AscendC::DataCopy(srcLocal, srcGlobal, count);
        AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);
        AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(EVENT_ID0);

        T scalar = 1.0;
        AscendC::Adds(dstLocal, srcLocal, scalar, count);

        AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(EVENT_ID0);
        AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(EVENT_ID0);
        AscendC::DataCopy(dstGlobal, dstLocal, count);
        AscendC::PipeBarrier<PIPE_ALL>();

        // 2. 计算完成后，把通信输入(computeGm)、输出(dstGm)地址写入共享变量
        __gm__ uint64_t* taskArgs0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[0]);
        __gm__ uint64_t* taskArgs1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[1]);
        AscendC::WriteGmBypassDCache<uint64_t>(taskArgs0, reinterpret_cast<uint64_t>(computeGm));
        AscendC::WriteGmBypassDCache<uint64_t>(taskArgs1, reinterpret_cast<uint64_t>(dstGm));
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

        // 3. 置event[0]，通知CCU开始通信
        __gm__ uint64_t* eventAddr0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[0]);
        __gm__ uint64_t* eventAddr1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[1]);
        AscendC::WriteGmBypassDCache<uint64_t>(eventAddr0, 1);
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

        // 4. 轮询event[1]，等待CCU通信完成
        while (1) {
            uint64_t waitEvent = AscendC::ReadGmBypassDCache(eventAddr1);
            AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();
            AscendC::Nop<800>();
            if (waitEvent == 1) {
                break;
            }
        }
    }
    ```

    > [!CAUTION]注意
    > 第1步的`V_MTE3`事件等待与`PipeBarrier`保证`Adds`结果写回GM后，才执行第2、3步写共享变量并置event[0]；若计算结果尚未落GM就置event[0]，CCU可能读到旧值。

    其中第4步的`while`轮询为串行等待CCU完成。本样例的Add + AllGather因此为"先算后通"串行执行，未真正重叠通信与计算；若需实现通算并行掩盖，可将该轮询替换为无依赖的计算逻辑（或采用多chunk流水线），用计算掩盖通信时延。

### Kernel内资源准备

- CCU Kernel与纯通信算子的区别在于：通信地址不再通过`load_arg`注入，而是在执行前从共享变量读取，并以事件与AI Core对齐时序。

    ```cpp
    // 等待AI Core写入共享变量并置event[0]
    ccu::array<ccu::event> events(ctx.arg->eventHandle, ctx.arg->eventNum);
    ccu::event_wait(events[0]);

    // 从共享变量读取通信输入、输出地址
    ccu::array<ccu::variable> vars(ctx.arg->varHandle, ctx.arg->varNum);
    ctx.input = vars[0];                   // 通信输入 = computeBuf
    ctx.output[ctx.arg->rankId] = vars[1]; // 通信输出 = recvBuf
    ```

- 随后绑定远端Variable单元、通过`ccu::load_arg`加载token与切分参数，与纯通信一致。

### 前同步

与纯通信算子一致，参见[基于CCU的通信算子开发指南·前同步](ccu_comm_op_dev.md#前同步)，本节不再展开。

### 算子执行

与纯通信AllGather一致（向各对端rank写数据完成发送，本地落位并等待数据到达完成接收），参见[基于CCU的通信算子开发指南·算子执行](ccu_comm_op_dev.md#算子执行)，本节不再展开。

### 后同步

与纯通信算子一致，参见[基于CCU的通信算子开发指南·后同步](ccu_comm_op_dev.md#后同步)，本节不再展开。

### 通知计算完成

- AllGather完成后置event[1]，通知AI Core退出轮询：

    ```cpp
    ccu::event_record(events[1]);
    ```

- 通知完成后，Kernel编排结束。Host侧分别通过`aclrtSynchronizeStream`同步`streamAiv`与`streamCcu`，即可读取`recvBuf`中的Add + AllGather结果。

## 注意事项

- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 计算与通信需分别通过`aclrtSynchronizeStream`同步`streamAiv`与`streamCcu`，二者之间的依赖仅由事件保证，应避免通过Stream间同步强制串行。
- 共享变量与事件由`asccomm_ccu_variable_alloc`、`asccomm_ccu_event_alloc`从CCU实例资源池分配，数量不得超过实例内相应资源份额。
- 一个CCU不能跨Die使用另一个IO Die的网络设备，Kernel内所用Channel须属同一Die。
- 样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。