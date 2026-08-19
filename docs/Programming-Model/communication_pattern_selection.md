# 通信方式分类及选择

在大规模分布式训练和推理业务中，通信方式会同时影响通信时延、带宽利用率、计算资源占用和组网范围。本节面向Ascend 950PR/Ascend 950DT，从驱动模式、通信引擎和通信协议三个相互关联的维度说明通信方式：驱动模式决定通信操作由当前Kernel直接发起，还是交由独立单元推进；通信引擎决定由哪类硬件执行通信任务；通信协议由组网结构、通信端口和可达链路决定。实际使用时，需要结合消息大小、性能目标、计算资源占用和系统拓扑综合判断。

## 驱动模式分类与选择

驱动模式描述当前算子Kernel与通信操作之间的发起关系，主要区别在于通信是否由当前Kernel直接执行。该选择会影响通信发起开销、计算资源占用以及通信与计算的并行方式。直驱模式侧重缩短发起路径，代理模式侧重将通信推进过程与当前Kernel解耦，对比结果如下：

**表1** 直驱模式与代理模式对比

| 模式 | 含义 | 主要特点 | 适用场景 |
| --- | --- | --- | --- |
| 直驱模式 | 当前算子Kernel直接发起通信操作。 | 发起路径短、时延低，但通信可能占用执行该Kernel的计算资源。 | 小数据低时延通信、细粒度通算融合和高性能定制算子。 |
| 代理模式 | 当前算子Kernel将通信请求交给独立的数据面单元，由其发起并推进通信。 | 可以将通信推进与当前Kernel解耦，但会增加请求转交和调度开销。 | 需要独立推进通信、减少计算核占用或实现较长时间通算并行的场景。 |

## 通信引擎分类与选择

确定通信的发起方式后，还需要回答“谁执行通信”，即选择实际承接通信任务的硬件主体。AI CPU+TS、AIV和CCU在调度开销、计算核占用、访存开销和适用消息规模方面各有侧重，因此通信引擎的选择应与时延、带宽和资源占用目标匹配。编程接口使用`CommEngine`枚举类型表示通信引擎，各引擎的枚举值和适用场景如下：

**表2** 不同通信引擎的适用场景

| 通信引擎 | 枚举值 | 优势 | 约束 | 适用场景 |
| --- | --- | --- | --- | --- |
| AI CPU+TS | `COMM_ENGINE_AICPU_TS` | 不占用计算核，通信效率高，适用于大数据高带宽场景。 | 通信静态开销较大，对小数据量通信场景不友好。 | 高带宽通信场景 |
| AIV | `COMM_ENGINE_AIV` | 低时延。 | 通信占用Vector计算核，需要多个Vector计算核才能打满通信带宽；通信算子与计算算子竞争计算核资源，可能互相影响。 | 低时延通信场景 |
| CCU | `COMM_ENGINE_CCU` | 减少访存带宽与计算核占用。<br>高带宽，低时延。 | 受限于片上资源，支持的通信域数量有限。 | 高带宽、低时延通信场景 |

## 通信协议分类与选择

通信协议描述数据在既定链路上的传输和访问方式。与驱动模式和通信引擎不同，通信协议不是编程时可以自由选择的参数，而是由通信两端的组网结构、端口类型和实际可达链路决定。控制面根据拓扑信息提供可用的Endpoint、Channel或链路资源，数据面使用与该链路匹配的协议和接口完成通信。Ascend 950PR/Ascend 950DT涉及的协议包括URMA、UBoE、UB Memory和RoCE，其中URMA、UBoE和UB Memory属于灵衢互联（UnifiedBus，UB）提供的通信能力，RoCE由外置DPU提供。

### URMA

URMA（Unified Remote Memory Access，统一远端内存访问）是UB提供的异步远端内存访问能力。通信操作以Jetty队列为入口，通过提交队列和完成队列异步推进，支持Write、Write with ImmediateData、Write with Notify、Read、Send、Send with ImmediateData，以及Atomic FetchAdd和CompareAndSwap等操作。URMA属于网络语义，编程时基于Channel调用相应的通信接口访问远端内存。

URMA提供以下两种传输模式：

- **CTP（Compact Transport Protocol）模式**：提供简化的传输层服务，不提供端到端可靠重传能力，但底层UB链路仍可提供链路级重传；该模式支持通过多条路径传输。编程接口在`CommProtocol`中使用`COMM_PROTOCOL_UB_CTP`表示该模式。
- **RTP（Reliable Transport Protocol）模式**：提供端到端可靠传输和可靠重传能力，并支持通过多个Transport Channel实现多路径传输。编程接口在`CommProtocol`中使用`COMM_PROTOCOL_UB_RTP`表示该模式。

CTP与RTP的使用取决于链路配置及其提供的传输服务。编程侧应使用控制面返回的协议类型，不应仅根据是否需要可靠传输而自行切换枚举值。

### UBoE

UBoE（UB over Ethernet）用于在Ethernet物理网络上承载UB协议栈，使UB通信可以借助以太网交换机扩展至更大的组网范围。Ascend 950系列芯片通过自身的UBoE能力接入以太网，UBoE Link与UB Link静态复用部分互联SerDes端口。UBoE链路承载基于网络语义的异步远端访问，编程接口在`CommProtocol`中使用`COMM_PROTOCOL_UBOE`表示该协议。

UBoE使用的是芯片提供的UB以太扩展链路。只有通信两端之间已经配置并打通UBoE链路时，控制面才会提供相应的通信资源。

### UB Memory

UB Memory是UB提供的同步访存能力，支持Write、Read、AtomicStore、AtomicLoad、AtomicSwap和AtomicCompareAndSwap等操作。AI Core或AI CPU发出的访存操作经UB Memory Decoder解析目标节点和地址，再通过UB端口发送至对端；对端完成地址转换和权限校验后访问目标内存。与URMA的队列式异步访问不同，UB Memory采用内存语义，远端通信内存映射到本地地址空间后，可以通过本地访存或拷贝接口访问。编程接口在`CommProtocol`中使用`COMM_PROTOCOL_UB_MEM`表示该协议。

UB Memory依赖通信两端之间可用的UB内存语义链路及已经建立的内存映射关系。Endpoint、Channel、通信内存以及网络语义和内存语义的具体编程方式，请参见[通信模型](./communication_model.md)。

### RoCE

RoCE（RDMA over Converged Ethernet）在Ethernet网络上提供RDMA通信能力。在Ascend 950PR/Ascend 950DT组网中，RoCE由外置DPU实现，NPU通过外置DPU接入以太网交换网络，并基于队列异步访问远端内存。编程接口在`CommProtocol`中使用`COMM_PROTOCOL_ROCE`表示该协议。

RoCE与UBoE都可以使用以太网交换网络，但对应的硬件通路不同：UBoE由芯片自身的UBoE能力承载UB协议栈，RoCE由外置DPU提供RDMA能力。实际使用哪一种协议由部署时建立的硬件通路和控制面提供的链路资源决定。

因此，编程时不应先指定协议再寻找链路，而应先查询通信两端的拓扑和可达链路，获取控制面提供的Endpoint或Channel，再按照资源中确定的协议调用匹配的数据面接口。如果通信引擎或接口不支持该协议，需要更换可用的通信引擎、接口或链路资源，而不能通过修改协议枚举绕过硬件和组网约束。同一NPU内部的数据搬运通常使用本地访存能力，不涉及NPU间通信协议；跨机柜或跨超节点通信还要求相应的交换网络和转发路径已经配置并可达。
