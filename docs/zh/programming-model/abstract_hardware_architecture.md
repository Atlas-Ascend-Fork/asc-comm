# 抽象硬件架构

本文介绍Ascend 950PR/Ascend 950DT的通信抽象硬件架构。内容按照通信规模由小到大展开：首先介绍单个NPU内参与通信的硬件组件，然后以两个NPU为例说明NPU间通信通路，最后介绍多个NPU如何通过不同的通信连接和组网拓扑构成更大规模的通信网络，为集合通信提供硬件基础。该抽象屏蔽了不必要的底层硬件实现细节，有助于开发者理解通信硬件组件、通信通路与组网架构之间的关系，降低使用Ascend C开发通信算子的门槛。

## 整体架构

NPU内部参与通信的硬件可以抽象为[**通信引擎**](#通信引擎)和[**存储单元**](#存储单元)。NPU间通信时，两端的通信引擎和存储单元通过互联网络协同完成数据传输，由此形成[**通信通路**](#通信通路)。当通信从两个NPU扩展到多个NPU时，还需要利用不同的通信连接和拓扑组织这些NPU，构成[**组网架构**](#组网架构)。[图1](#fig-communication-hardware)展示了NPU内部通信硬件如何协同完成数据传输，以及多个NPU如何通过组网实现互联。

<a id="fig-communication-hardware"></a>

**图 1** 通信抽象硬件架构图

![通信抽象硬件架构图](./figures/comm_hardware.png)

[图1](#fig-communication-hardware)所示组网是Ascend 950PR/Ascend 950DT推荐的一种标准组网形态，由AI机柜和超节点逐级构成：一个超节点包含多个AI机柜，多个超节点通过上层网络进一步互联。为说明组网中通信端点的内部结构，图中展开超节点1内的AI机柜1，并选取NPU 0和NPU 1作为一对通信端点，展示单个NPU内参与通信的硬件组件以及两个NPU之间的点对点通信通路；其余AI机柜和超节点仅保留组网关系。图中以NPU 0向NPU 1的数据传输为例，实际通信支持沿相应通路在两个NPU之间互相传输数据。

在该组网中，UB（UnifiedBus，灵衢互联）提供NPU、DPU与灵衢Switch之间的互联能力，灵衢Switch负责UB网络中的数据交换和转发；DPU（Data Processing Unit，数据处理单元）提供网络接入和数据处理能力，外置DPU还可通过RoCE（RDMA over Converged Ethernet）在以太网上承载RDMA通信。上述设备和互联方式共同建立NPU之间的可达路径，端点内部的通信硬件及其数据通路将在下文展开介绍。

## 通信硬件

NPU内部参与通信的硬件可以归纳为通信引擎和存储单元：通信引擎负责执行通信算法或提交通信任务，存储单元用于保存和中转通信输入、输出及中间数据。

### 通信引擎

通信引擎是执行通信算法或承接通信任务的硬件主体，各引擎的执行主体和硬件职责如下表。

**表 1** 通信引擎

| 通信引擎 | 执行主体 | 主要功能 |
| --- | --- | --- |
| AIV | Vector Core | 执行通信Kernel中的算法逻辑，以及数据处理、搬运和归约等操作。 |
| AI CPU | AI CPU | 采用[AI CPU+TS执行模式](./execution_model.md#aicpu_ts直驱执行机制)。STARS是Device侧的任务调度器，负责调度AI CPU Kernel以及提交到任务队列中的通信任务。AI CPU Kernel由STARS调度执行后，向任务队列提交数据搬运等通信任务，再由STARS调度到相应的执行器。 |
| CCU（Collective Communication Unit，集合通信加速单元） | 集合通信专用硬件引擎 | 执行预置通信指令，并使用内部CCU Buffer完成数据暂存、搬运、同步和片上归约等操作。 |

### 存储单元

参与通信的存储单元包括Global Memory、Unified Buffer和CCU Buffer。

- **Global Memory**：GM是Device侧的全局存储，用于保存通信输入、输出和中间数据，也是各通信引擎共享通信数据的主要存储空间。
- **Unified Buffer**：Unified Buffer是Vector Core（AIV）内部用于矢量计算单元的核心内部存储单元。
- **CCU Buffer**：CCU Buffer位于CCU内部，是CCU进行数据中转和片上归约时使用的本地片上缓存。它可以暂存待发送的数据、接收远端数据或保存归约中间结果，使数据在接收、归约和继续发送之间尽量保留在片上，减少集合通信过程中对GM的重复读写，并为计算任务留出更多GM访问带宽。CCU Buffer属于容量受限的分片缓存，适合分块处理通信数据，不能替代GM保存大规模通信数据。

## 通信通路

根据参与通信的硬件引擎，NPU间通信通路可以分为以下三类：

- **AIV通信通路**：`本端AIV的Unified Buffer -> 本端GM -> 对端GM -> 对端AIV的Unified Buffer`。
- **AI CPU通信通路**：`本端AI CPU -> 本端GM -> 对端GM -> 对端AI CPU`。
- **CCU通信通路**：
  - `本端CCU的CCU Buffer -> 本端GM -> 对端GM -> 对端CCU的CCU Buffer`
  - `本端CCU的CCU Buffer -> 对端GM -> 对端CCU的CCU Buffer`

这些通路中的跨NPU访问均以对端GM为目标，本端和对端CCU Buffer之间不能直接互访。不同通信引擎的特点、约束和适用场景，请参见[通信方式分类及选择](./communication_pattern_selection.md#通信引擎分类与选择)。

## 组网架构

随着模型规模和计算规模扩大，参与通信的NPU数量随之增加，需要通过组网将点对点连接组织成更大规模的通信网络。[图1](#fig-communication-hardware)展示了组网从AI机柜到超节点的逐级扩展关系：超节点1中的AI机柜1展开了NPU、CPU、DPU和交换网络，其余AI机柜以相同结构接入上层交换网络；超节点N省略内部结构，按照与超节点1相同的方式扩展并建立超节点间连接。

下面从通信连接和组网拓扑两个方面进行介绍：
- 通信连接说明不同设备和组网层级使用何种链路与交换网络建立可达路径。
- 组网拓扑说明这些连接如何组织，并决定通信两端是否具备直连链路、需要经过多少转发节点以及网络的扩展方式。

### 通信连接

[图1](#fig-communication-hardware)是Ascend 950推荐的一种标准组网形态，根据通信范围，其连接可以分为AI机柜内、超节点内和超节点间三类。三类连接分别承担设备接入、跨机柜汇聚和跨超节点扩展功能，共同构成本端到对端的硬件可达路径：

- **AI机柜内连接**：NPU、CPU及支持UB的DPU接入L1灵衢交换网络，在同一机柜内建立UB连接。外置DPU通过PCIe连接CPU，并提供RoCE通路，使机柜可以接入以太网RDMA网络。
- **超节点内连接**：各AI机柜通过上联链路接入L2灵衢交换机或以太网交换机。上层交换网络汇聚多个AI机柜，跨机柜数据经过接入层和汇聚层到达目标NPU，不要求所有NPU之间建立物理直连。
- **超节点间连接**：UBoE（UB over Ethernet）通过以太网交换网络延伸UB通信，外置DPU提供的RoCE通路也可以连接不同超节点。两种通路对应不同的端口和协议栈，实际使用哪一种取决于部署的端口、交换网络和可达链路。

上述通信连接共同建立本端到对端的硬件可达路径。可用协议由端口类型和实际链路确定，不能脱离组网任意选择；协议能力及对应编程枚举请参见[通信方式分类及选择](./communication_pattern_selection.md#通信协议分类与选择)。

### 组网拓扑

UB组网可采用的拓扑结构包括：Clos、nD-FullMesh和nD-Mesh。

#### Clos

Clos是一种基于多级交换的拓扑。NPU或局部拓扑单元接入底层交换设备，底层交换设备再连接更高层交换设备，跨分支通信通过交换网络完成。

<a id="fig-clos-topology"></a>

**图 2** Clos拓扑

![Clos拓扑](./figures/clos_topology.png)


#### nD-FullMesh

FullMesh即全互联拓扑，同一FullMesh内的任意两个NPU均直接连接，无需经过其他NPU转发。

nD-FullMesh在FullMesh的基础上按多个逻辑维度扩展组网。1D-FullMesh由一组NPU两两直接连接构成；扩展到更高维度时，将低一维FullMesh复制为多个拓扑单元，并将不同单元中位置对应的NPU在新增维度上两两直接连接，逐级形成2D、3D直至nD-FullMesh。与将全部NPU组成一个单一FullMesh不同，nD-FullMesh在各维度内分别建立全互联关系，通过增加维度扩大组网规模。

<a id="fig-nd-fullmesh-topology"></a>

**图 3** nD-FullMesh拓扑

![nD-FullMesh拓扑](./figures/nd_fullmesh_topology.png)

#### nD-Mesh

nD-Mesh将NPU按n个逻辑维度排列，每个NPU与各维度上的相邻NPU直接连接；非相邻NPU之间的数据沿一个或多个维度逐跳转发。

<a id="fig-nd-mesh-topology"></a>

**图 4** nD-Mesh拓扑

![nD-Mesh拓扑](./figures/nd_mesh_topology.png)

Clos、nD-FullMesh和nD-Mesh既可以独立组网，也可以分层组合，具体组合取决于设备端口和部署规模。
