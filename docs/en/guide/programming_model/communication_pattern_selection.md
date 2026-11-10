# Communication Pattern Classification and Selection

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-05T01:49:59.114Z pushedAt=2026-11-09T06:49:19.130Z nl=2 -->

In large-scale distributed training and inference, the communication method simultaneously affects communication latency, bandwidth utilization, computing resource usage, and networking scope. This section describes the communication method from three interrelated dimensions: drive mode, communication engine, and communication protocol:

- The drive mode determines whether a communication operation is initiated directly by the current kernel or advanced by an independent unit.
- The communication engine determines which type of hardware executes the communication task.
- The communication protocol is determined by the networking structure, communication ports, and reachable links.

## Drive Mode Classification and Selection

The drive mode describes the initiation relationship between the current operator kernel and the communication operation. The main difference lies in whether the communication is directly executed by the current kernel. This choice affects the communication initiation overhead, the occupation of computing resources, and the parallelism between communication and computation. The direct-drive mode focuses on shortening the initiation path, while the proxy mode focuses on decoupling the communication progression from the current kernel. The comparison is as follows:

**Table 1** Comparison between the direct-drive mode and the proxy mode

| Mode | Meaning | Key Features | Application Scenario |
| --- | --- | --- | --- |
| Direct-drive mode | The current operator kernel directly initiates the communication operation. | Short initiation path and low latency, but the communication may occupy the computing resources used to execute the kernel. | Low-latency communication and high-performance custom operators. |
| Proxy mode | The current operator kernel hands over the communication request to an independent data plane unit, which initiates and progresses the communication. | Decouples communication progression from the current kernel, but adds request handover and scheduling overhead. | Scenarios that require independent communication progression, reduced compute core occupation, or long-duration parallelism between communication and computation. |

## Communication Engine Classification and Selection

After determining how communication is initiated, you also need to answer "who executes the communication", that is, select the hardware entity that actually undertakes the communication task. AI CPU+TS, AIV, and CCU differ in scheduling overhead, compute core occupancy, memory access overhead, and applicable message sizes. Therefore, the selection of the communication engine should match the latency, bandwidth, and resource occupancy objectives. The programming API uses the `CommEngine` enum type to represent the communication engine. The enum values and applicable scenarios of each engine are as follows:

**Table 2** Applicable scenarios of different communication engines

| Communication Engine | Enum Value | Advantage | Constraint | Application Scenario | Supported Product Model |
| --- | --- | --- | --- | --- | --- |
| AI CPU+TS | `COMM_ENGINE_AICPU_TS` | Does not occupy compute cores, offers high communication efficiency, and is suitable for large-data, high-bandwidth scenarios. | Has high static communication overhead and is not suitable for small-data communication scenarios. | High-bandwidth communication scenarios. | Ascend 950PR&950DT products |
| AIV | `COMM_ENGINE_AIV` | Low latency. | Communication occupies Vector compute cores, and multiple Vector compute cores are required to fully utilize the communication bandwidth. Communication operators compete with compute operators for compute core resources, which may cause mutual interference. | Low-latency communication scenarios. | Ascend 950PR&950DT products |
| CCU | `COMM_ENGINE_CCU` | Reduces memory access bandwidth and compute core usage.<br>High bandwidth and low latency. | Limited by on-chip resources, the number of supported communicators is limited. | High-bandwidth, low-latency communication scenarios. | Ascend 950PR&950DT products |

## Communication Protocol Classification and Selection

A communication protocol defines how data is transmitted and accessed over a given link. Unlike the drive mode and communication engine, a communication protocol is not a parameter that can be freely selected independently of the link. Instead, it is determined by the communication ports and reachable links in the actual physical networking. During programming, use the protocol type returned by the control plane and call the data plane APIs that match that protocol. The Ascend 950PR&950DT products involve UB_CTP, UB_RTP, UB Memory, UBoE, and RoCE. Their meanings are as follows:

**Table 3** Meanings and programming enums of communication protocols

| Communication Protocol | Meaning | Enum Value |
| --- | --- | --- |
| UB_CTP | A lightweight transfer mode of URMA (Unified Remote Memory Access). It asynchronously accesses remote memory based on queues and provides link-level reliable transmission and congestion control through the underlying link. | `COMM_PROTOCOL_UB_CTP` |
| UB_RTP | A reliable transfer mode of URMA. It asynchronously accesses remote memory based on queues and provides end-to-end reliable transmission and reliable retransmission. | `COMM_PROTOCOL_UB_RTP` |
| UB Memory | A synchronous memory semantic provided by UB. After the remote communication memory is mapped to the local address space, it can be accessed through Load, Store, or local copy. | `COMM_PROTOCOL_UB_MEM` |
| UBoE | UB over Ethernet. It carries the UB protocol stack over the Ethernet physical network, allowing UB communication to be extended through Ethernet switching networks. | `COMM_PROTOCOL_UBOE` |
| RoCE | RDMA over Converged Ethernet. Provided by an external DPU, it asynchronously accesses remote memory based on queues over the Ethernet network. | `COMM_PROTOCOL_ROCE` |
