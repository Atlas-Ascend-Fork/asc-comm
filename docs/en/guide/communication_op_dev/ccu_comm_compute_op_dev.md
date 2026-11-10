# CCU MC2 Operator Development Guide

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-05T01:35:17.386Z pushedAt=2026-11-09T06:49:19.125Z nl=2 -->

## Overview

An MC2 operator fuses the computation on the AI Core and the collective communication on the CCU into a single operator. Based on the [add_allgather](../../../../examples/ccu/ccu_direct/12_add_allgather/README.md) sample, this guide uses Add + AllGather as an example to describe how to develop an MC2 operator through `<<<>>>` direct call.

## Supported Scope

| Item | Description |
| --- | --- |
| Communication protocol | `COMM_PROTOCOL_UB_CTP` |
| Communication engine | `COMM_ENGINE_CCU` |

<!-- npu="950" id1 -->
> [!NOTE] Note
> This sample supports the Ascend 950PR&950DT products. CANN 9.1.0 or later is required. A single-device environment supports compilation verification only, and at least two NPUs are required for actual running.
<!-- end id1 -->

## Overall Process

Using the [add_allgather](../../../../examples/ccu/ccu_direct/12_add_allgather/README.md) sample as an example, the complete implementation process of an MC2 operator consists of two phases: host-side resource preparation and kernel-side communication execution. After communication completion, resources are destroyed.

**Host-side resource preparation** (control plane): completes the resource preparation, task dispatch, and resource reclamation required for MC2 task execution on the host side.

1. Initialize the operating environment: initialize the ACL and HCCL communicator, and obtain the number of NPUs in the current environment.
2. Allocate memory and initialize input data: create a rank for each device and initialize the input buffer.
3. Request CCU resources: request CCU channel, CCU instance, CCU variable, and CCU event resources based on the HCCL communicator.
4. Dispatch the AI Core compute task: directly call the AI Core vector kernel through `vector_add<<<1, nullptr, streamAiv>>>` to generate the local computation result.
5. Obtain the memory token and prepare task parameters: Call `HcommCcuGetMemToken` to obtain the memory token of `computeBuf`, and prepare the CCU task parameters.
6. Dispatch the CCU communication task: Directly call the CCU kernel through `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>` to complete AllGather.
7. Destroy resources: Destroy the HCCL communicator, streams, and device-side memory.

**Kernel-side communication execution** (data plane): After the tasks are dispatched to the streams, AI Core and CCU execute on the compute stream and the communication stream respectively. The CCU side completes data transfer and synchronization following the communication programming paradigm of pre-synchronization, operator execution, and post-synchronization, and orchestrates the synchronization between compute and communication through shared variables and events.

1. AI Core compute kernel: Execute the local Add computation. After completion, it writes the communication input and output addresses into the shared variables and sets an event to notify CCU to start communication, then polls the event to wait for CCU communication completion.
2. In-kernel resource preparation: Wait for the event notification from AI Core, read the communication input and output addresses from the shared variables, then bind the remote variables and loads the task parameters.
3. Pre-synchronization: Write the local communication memory information (output address and token) to the peer with a synchronization signal, and wait for the peer to be ready before performing data transfer.
4. Operator execution: Initiate cross-rank data transfer and local copy within the current rank, and wait for all transfers to complete.
5. Post-synchronization: Notify the peer that the transfer is complete, and wait for all peers to complete.
6. Notify compute completion: After AllGather completes, set the event to notify AI Core to exit polling.

The association between the host side and the kernel side is reflected as follows: after the host side completes resource preparation, it dispatches the AI Core and CCU kernel direct calls to the compute and communication streams respectively in steps 4 and 6, and delivers the channel handle, shared variables and event handles, and task parameters (**taskArgs**, **kernelArg**) along with the tasks. On the kernel side, after the AI Core completes computation, it writes the communication address into the shared variable and sets the event. The CCU side loads the task parameters through `ccu::load_arg`, binds the variable through `ccu::get_res_by_channel`, and then performs pre-synchronization, operator execution, and post-synchronization in sequence. After completion, it sets the event to notify the AI Core to exit polling. The shared variables and events requested in step 3, as well as the dispatch of the compute and communication kernels to two streams in steps 4 and 6, are the key differences between MC2 and communication-only operators: AI Core computation and CCU communication are carried by two streams, the communication address is passed through shared variables, and bidirectional synchronization is achieved through events.

The subsequent content of this chapter is organized according to this process, and the key code snippets are all taken from the [add_allgather](../../../../examples/ccu/ccu_direct/12_add_allgather/README.md) sample.

## Host-Side Resource Preparation

### Initializing the Operating Environment

- Initializing ACL, generating **rootInfo**, and initializing the communicator for each rank are consistent with communication-only operators. For details, see ["Initializing the Operating Environment" in the CCU Communication Operator Development Guide](ccu_comm_op_dev.md#initializing-the-operating-environment). This section does not expand on them.

- Create two streams for computing and communication: the AI Core compute task and the CCU communication task are carried by `streamAiv` and `streamCcu` respectively, enabling parallel communication and computing:

    ```cpp
    // Create two task streams for computing and communication to enable parallel communication and computing.
    aclrtStream streamAiv;
    ACLCHECK(aclrtCreateStream(&streamAiv));
    aclrtStream streamCcu;
    ACLCHECK(aclrtCreateStream(&streamCcu));
    ```

### Allocating Memory and Initializing Input Data

- Compared with a communication-only operator, an MC2 operator additionally requests a `computeBuf` to store the compute result (that is, the communication input).

    ```cpp
    // computeBuf stores the compute result (communication input), and recvBuf stores the AllGather result
    ACLCHECK(aclrtMalloc(&computeBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ```

- Input data initialization and host-to-device copy are consistent with those of a communication-only operator, and are not introduced here.

### Allocating CCU Resources

- The allocation of CCU channels and CCU instances is consistent with that of communication-only operators. For details, see ["Allocating CCU Resources" in the CCU Communication Operator Development Guide](ccu_comm_op_dev.md#allocating-ccu-resources). This section does not introduce it.

- Allocating shared variables and events: AI Core and CCU belong to different streams, so dependencies cannot be established through intra-stream ordering. An MC2 operator implements cross-engine data interaction and synchronization through shared variables and events allocated from the CCU instance resource pool on the host side: AI Core directly reads and writes variables in GM and sets events, while the CCU kernel reads variables and waits for events.

    ```cpp
    // Allocate MAPPED_VARIABLE_NUM shared variables: store the communication input (computeBuf) and output (recvBuf) addresses respectively.
    CcuMappedAddrs mappedAddrs{};
    ccu_variable_handle varHandle{};
    HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_variable_alloc(insHandle, ccuDieId, MAPPED_VARIABLE_NUM, &varHandle)));
    for (uint32_t i = 0; i < MAPPED_VARIABLE_NUM; i++) {
        HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_variable_get_addr(varHandle, i, &mappedAddrs.startVa[i])));
    }

    // Allocate EVENT_NUM events: event[0] notifies CCU to start communication, and event[1] notifies AIV of communication completion.
    ccu_event_handle eventHandle{};
    HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_event_alloc(insHandle, ccuDieId, EVENT_NUM, &eventHandle)));
    for (uint32_t i = 0; i < EVENT_NUM; i++) {
        HCCLCHECK(ConvertCcuToHccl(asccomm_ccu_event_get_addr(eventHandle, i, &mappedAddrs.eventVa[i])));
    }
    ```

    The `asccomm_ccu_*` APIs return `CcuResult`. In the sample, the result is converted to `HcclResult` through `ConvertCcuToHccl` and then checked by `HCCLCHECK`.

    The VA address of the variable (`mappedAddrs.startVa`) is dispatched along with the AI Core kernel parameters for writing the communication address. The handles of the variable and event (`varHandle` and `eventHandle`) are written into `kernelArg` and dispatched along with the CCU kernel for reading the address and waiting for/setting events.

### Dispatching the AI Core Compute Task

- Directly call the AI Core vector kernel through `vector_add<<<1, nullptr, streamAiv>>>` to dispatch the Add compute task to the compute stream and generate the local computation result:

    ```cpp
    // Directly call the AI Core kernel through <<<>>> and dispatch it to the compute stream.
    vector_add<float, 256><<<1, nullptr, streamAiv>>>(sendBuf, computeBuf, recvBuf, mappedAddrs);
    ```

### Obtaining the Memory Token and Preparing Task Parameters

- `computeBuf` serves as the communication input, and its memory token is obtained through `HcommCcuGetMemToken`. Since the communication input/output addresses are now passed through shared variables, `taskArgs` no longer contains addresses and only needs the token and split parameters:

    ```cpp
    // token is the memory token of computeBuf
    uint64_t token = 0;
    HCCLCHECK(ConvertCcuToHccl(HcommCcuGetMemToken(reinterpret_cast<uint64_t>(computeBuf), inputSize, &token)));

    uint64_t taskArgs[CCU_DIRECT_TASK_ARG_NUM] = {
        token, 0, inputSize * rankId, inputSize, goSize[0], goSize[1], goSize[2], goSize[3],
    };

    // In addition to the channel handle, rankSize, and rankId, kernelArg also carries the shared variable and event handles
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

### Dispatching the CCU Communication Task

- Directly call the CCU kernel through `CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>` to dispatch the AllGather communication task to the communication stream. The `<<<>>>` syntax, parameters, and function signature of the CCU kernel are consistent with those of a communication-only operator. Its SQE parameters are `sqeArgs1` to `sqeArgs8`, which correspond one-to-one to **taskArgs** (the communication address is now passed through a shared variable, so **taskArgs** does not contain an address item):

    ```cpp
    // Directly call the CCU kernel through <<<>>> to dispatch it to the communication stream. tag is the kernel handle cache tag.
    const std::string launchTag = "CcuAllGatherMesh1DMem2MemKernel_rank_" + std::to_string(rankId);
    uint64_t tag = asccomm_ccu_get_launch_hash_tag(launchTag.c_str());

    asccomm_ccu_schd schd = {1, 0, dieMask, tag};
    CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>(
        taskArgs[0], taskArgs[1], taskArgs[2], taskArgs[3], taskArgs[4], taskArgs[5], taskArgs[6], taskArgs[7],
        kernelArg.get());
    ```

    Here, `asccomm_ccu_get_launch_hash_tag` generates the handle cache tag in `schd` based on the tag string: when it is non-zero, repeated calls with the same tag reuse the registered kernel handle, avoiding repeated registration. The sample generates the tag based on **rankId** to ensure that the tag is unique for each rank.

### Destroying Resources

- After communication completion, destroy the communicator, release the device memory, destroy the stream, and reset the device:

    ```cpp
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtFree(computeBuf));
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    ACLCHECK(aclrtDestroyStream(streamCcu));
    ACLCHECK(aclrtDestroyStream(streamAiv));
    ACLCHECK(aclrtResetDevice(device));
    ```

## Kernel-Side Communication Execution

### AI Core Compute Kernel

- `vector_add` performs the following in sequence: local Add computation, writing the communication input/output addresses into the shared variable, setting event[0] to notify the CCU to start communication, and polling event[1] to wait for CCU communication completion.

    ```cpp
    template <typename T, uint64_t count>
    __global__ __vector__ void vector_add(
        __gm__ void* srcGm, __gm__ void* computeGm, __gm__ void* dstGm, CcuMappedAddrs mappedAddrs)
    {
        // 1. Local Add computation: srcGm -> computeGm (Adds on the scalar 1.0)
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

        // 2. After the computation is complete, write the communication input (computeGm) and output (dstGm) addresses into the shared variable
        __gm__ uint64_t* taskArgs0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[0]);
        __gm__ uint64_t* taskArgs1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.startVa[1]);
        AscendC::WriteGmBypassDCache<uint64_t>(taskArgs0, reinterpret_cast<uint64_t>(computeGm));
        AscendC::WriteGmBypassDCache<uint64_t>(taskArgs1, reinterpret_cast<uint64_t>(dstGm));
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

        // 3. Set event[0] to notify the CCU to start communication.
        __gm__ uint64_t* eventAddr0 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[0]);
        __gm__ uint64_t* eventAddr1 = reinterpret_cast<__gm__ uint64_t*>(mappedAddrs.eventVa[1]);
        AscendC::WriteGmBypassDCache<uint64_t>(eventAddr0, 1);
        AscendC::DataSyncBarrier<AscendC::MemDsbT::ALL>();

        // 4. Poll event[1] to wait for CCU communication completion.
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

    > [!CAUTION] Caution
    > The `V_MTE3` event wait and `PipeBarrier` in step 1 ensure that the `Adds` result is written back to GM before steps 2 and 3 write the shared variable and set event[0]. If event[0] is set before the computation result is written to GM, the CCU may read a stale value.

    The `while` polling in step 4 is a serial wait for CCU completion. Therefore, the Add + AllGather in this sample is executed serially in a "compute first, then communicate" manner, without truly overlapping communication and computation. To achieve communication-computing convergence that hides latency, you can replace this polling with computation logic that has no dependency (or use a multi-chunk pipeline) to hide the communication latency with computation.

### Preparing Resources in the Kernel

- The difference between a CCU kernel and a communication-only operator is that the communication address is no longer injected through `load_arg`; instead, it is read from the shared variable before execution, and the timing is aligned with the AI Core through events.

    ```cpp
    // Wait for the AI Core to write the shared variable and set event[0].
    ccu::array<ccu::event> events(ctx.arg->eventHandle, ctx.arg->eventNum);
    ccu::event_wait(events[0]);

    // Read the communication input and output addresses from the shared variable.
    ccu::array<ccu::variable> vars(ctx.arg->varHandle, ctx.arg->varNum);
    ctx.input = vars[0];                   // Communication input = computeBuf
    ctx.output[ctx.arg->rankId] = vars[1]; // Communication output = recvBuf
    ```

- Then bind the remote variable unit and load the token and split parameters through `ccu::load_arg`, which is consistent with communication-only operators.

### Pre-Synchronization

The pre-synchronization process is consistent with that of communication-only operators. For details, see [CCU Communication Operator Development Guide* · Pre-synchronization](ccu_comm_op_dev.md#pre-synchronization). This section does not introduce it.

### Operator Execution

The execution process is consistent with that of the communication-only AllGather (writing data to each peer rank completes sending, and local placement plus waiting for data arrival completes receiving). For details, see ["Operator Execution" in the CCU Communication Operator Development Guide](ccu_comm_op_dev.md#operator-execution). This section does not introduce it.

### Post-Synchronization

The post-synchronization process is consistent with that of communication-only operators. For details, see ["Post-Synchronization" in the CCU Communication Operator Development Guide](ccu_comm_op_dev.md#post-synchronization). This section does not introduce it.

### Notifying Compute Completion

- After AllGather completes, event[1] is set to notify the AI Core to exit polling:

    ```cpp
    ccu::event_record(events[1]);
    ```

- After the notification is complete, the kernel orchestration ends. On the host side, synchronize `streamAiv` and `streamCcu` through `aclrtSynchronizeStream` respectively. Then, the Add + AllGather result in `recvBuf` can be read.

## Precautions

- Before running, you must set `HCCL_OP_EXPANSION_MODE=CCU_SCHED`.
- Compute and communication must be synchronized through `aclrtSynchronizeStream` for `streamAiv` and `streamCcu` respectively. The dependency between them is guaranteed only by events. Avoid forcing serialization through inter-stream synchronization.
- Shared variables and events are allocated from the CCU instance resource pool through `asccomm_ccu_variable_alloc` and `asccomm_ccu_event_alloc`. The number must not exceed the corresponding resource quota within the instance.
- A CCU cannot use the network device of another IO die across dies. The channels used in the kernel must belong to the same die.
- The sample uses all NPU devices visible in the environment. The number of devices must not exceed `CCU_MAX_RANK_SIZE`.
