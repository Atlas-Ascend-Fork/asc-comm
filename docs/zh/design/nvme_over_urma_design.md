# NPU 直访远端 NVMe 盘设计方案（设计草案）

> **文档性质**：本文是设计提案（design proposal），描述的能力**均未在本仓实现**，不代表当前已支持的功能。
> 文中涉及的现有接口以 `include/aicore/hcomm/hcomm.h` 和 `include/aicore/ain/ain.h` 为准。
>
> **适用范围**：Ascend 950PR / Ascend 950DT，网络走 URMA（UBC_CTP）。

## 1. 背景

目标场景：NPU（AIV 直驱）绕过 Host CPU，直接访问远端 NVMe 盘，数据路径为 NVMe over Fabric，网络部分走 URMA。

经过分析，当前仓的能力与该场景之间存在两个结构性缺口：

1. **WQE 语义缺口**：设备侧 URMA 路径只暴露单边操作（`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`/`AtomicCAS`、`WriteReduceNbi`，见 `src/aicore/hcomm/impl/platform_v310/hcomm_aiv_urma_def.h`），没有 SEND/RECV 双边语义。而 NVMe-oF 的命令 capsule 投递本质上是双边操作（target 需要预投命令槽位、被到达事件唤醒、做流控）。
2. **对端模型缺口**：Host 侧建链流程（`src/aicore/hcomm/host/hcomm_host_impl.h`）假设对端是暴露 HBM 对称窗口的 NPU（EID + 对端 MR 表），不是 NVMe-oF target 或存储端点。

此外，URMA/UB 不在标准 NVMe-oF 传输绑定（RDMA/TCP/FC）之列，因此无论哪套方案，对端都不能是"任意标准 NVMe-oF target"，必须有存储侧的配合。

两套方案的共同基础：**数据面**复用本仓现有的单边引擎（`WriteNbi`/`ReadNbi`/`MakeBatchHandle`/`BatchCommit`/`Drain`），引擎和 WQE 格式代码均不需要修改；差别在**控制面**（命令投递）由谁、以什么语义完成。

## 2. 共同前置验证（两方案实施前都必须确认）

| 编号 | 待确认项 | 影响的方案 | 确认途径 |
| --- | --- | --- | --- |
| V1 | 950 UB 网卡的 AIV 直驱模式是否支持 SEND/RECV WQE | 方案一 | 网卡固件/驱动文档或海思接口人 |
| V2 | 目标存储是否支持 URMA/UB 网络接入 | 两者 | 存储产品规格 |
| V3 | target 侧是否有可配合的协议栈（方案一）或内存窗口能力（方案二） | 两者 | 存储产品接口人 |

## 3. 方案一：双边语义——NPU 作为 NVMe-oF initiator

### 3.1 设计目标

NPU 侧实现 NVMe-oF initiator 能力：connect / admin 队列 / IO 队列 / completion 全流程，命令 capsule 通过双边语义投递，target 侧消费 capsule 并终结到 NVMe 盘。

由于 URMA 无标准 binding 且设备侧大概率没有 SEND/RECV WQE（待 V1 确认），本方案采用**单边原语模拟双边语义**的私有 binding。

### 3.2 总体分层

```text
┌────────────────────────── NPU ──────────────────────────┐
│ 算子 Kernel                                               │
│   └── NVMe IO 热路径（新增，device 侧）                     │
│         └── capsule 传输层（新增，device 侧）               │
│               └── AscendC::Hcomm<COMM_PROTOCOL_UBC_CTP>  │  ← 现有，不改动
├────────────────────────── Host ─────────────────────────┤
│ NVMe-oF initiator 协议状态机（新增，用户态）                 │
│   connect / identify / 建 IO 队列 / keep-alive / 重连      │
│ 存储端点建链适配（新增）+ 本地 MR key 导出（新增接口）        │
└──────────────────────────────────────────────────────────┘
              │  URMA（capsule 环 + notify + 单边数据面）
┌────────────────────────── 存储侧 ────────────────────────┐
│ 定制 target 栈（跑在存储侧 CPU/DPU）：消费 capsule，        │
│ 执行 NVMe 命令，单边 RDMA 搬数据，写回 completion           │
└──────────────────────────────────────────────────────────┘
```

### 3.3 修改点 A：capsule 传输层（核心新代码，device 侧）

用单边原语复刻双边语义：

- **命令通道**：在 target 侧内存约定 SQ ring（命令槽位环形队列，每槽 64B capsule）。NPU 提交命令：
  1. `WriteNbi` 将 capsule 写入空闲槽位；
  2. `AtomicFAA` 更新远端队尾指针；
  3. `WriteWithNotifyNbi` 写 notify 地址，作为门铃（替代 SEND 到达事件）。
- **完成通道**：CQE ring 放在本地 GM，target 单边写 completion + notify 回来；NPU 用 `Ain` 的 `WaitSignal` 或轮询标志位等待。
- **信用流控**：target 预投槽位数即队列深度，本地维护 free-slot 计数，耗尽时等待 CQE 回收槽位。
- 代码落点：新增 `src/aicore/nvmeof/` 目录，内部复用 `Hcomm<COMM_PROTOCOL_UBC_CTP>` 的 WQE 提交与完成机制，`src/aicore/hcomm/` 下代码不改动。

### 3.4 修改点 B：initiator 协议状态机（新增，Host 用户态）

- discovery 裁剪为静态配置（NQN、EID 表）；
- connect / identify / 创建 IO 队列 / keep-alive / 连接重建，走与修改点 A 相同的 capsule 通道；
- 不使用内核 nvme-fabrics（只认标准 RDMA/TCP binding，接不了 URMA），自研用户态模块；
- 管理面慢路径全在 Host，device 只跑 IO 热路径（组 SQE → capsule → 等 completion）。

### 3.5 修改点 C：Host 建链扩展（改造现有代码）

- 现有 `src/aicore/hcomm/host/hcomm_host_impl.h` 面向 NPU 对端（从 ChannelEntity 解析对端 MR 表），需新增"存储端点"通道类型：
  - 存储侧 EID 交换与建链（依赖底层 hcomm/URMA 驱动允许连非 NPU 端点）；
  - target 侧 SQ ring 地址、MR 信息导入；
  - **本地 GM buffer 的 tokenId/tokenValue 导出接口**（target 执行 RDMA 数据搬运需要；现有代码只缓存远端 token，无本地 key 导出能力）。

### 3.6 修改点 D：可靠性与块层语义

- 命令超时、重试、abort、队列冻结/重建全套错误处理（新写）；
- NVMe 错误码到块层错误码的映射。

### 3.7 约束与依赖

- 依赖 V1：若无 SEND/RECV WQE，按本文模拟方案实施，控制面每条命令多一次 notify 往返；
- 依赖 V3：target 必须是定制栈或华为 UB 存储提供的等价 capsule 通道，**无法对接标准内核 nvmet / SPDK target**；
- 本方案是私有 binding，不构成标准 NVMe-oF 互通能力。

## 4. 方案二：单边语义——target 侧终结 NVMe（盘即内存）

### 4.1 设计目标

不在 fabric 上跑 NVMe-oF 协议。存储系统（UB 盘框/带控制器的存储）内部完成 LBA→NAND 翻译，通过 URMA 把容量暴露为可注册的远端内存窗口；NPU 纯单边读写，NVMe 协议在存储盒子内部终结。

### 4.2 总体分层

```text
┌────────────────────────── NPU ──────────────────────────┐
│ 算子 Kernel                                               │
│   └── 块映射层（新增，device 侧）：(ns, LBA) → 远端地址     │
│         └── AscendC::Hcomm<COMM_PROTOCOL_UBC_CTP>        │  ← 现有，不改动
├────────────────────────── Host ─────────────────────────┤
│ 存储端点建链适配（新增）：EID + 存储 MR token 表导入         │
│ 命名空间/映射表管理（新增，下发到 GM）                        │
└──────────────────────────────────────────────────────────┘
              │  URMA 纯单边 Read/Write（+ flush notify）
┌────────────────────────── 存储侧 ────────────────────────┐
│ 存储控制器：内存窗口 ↔ LBA 映射 ↔ NVMe 盘                   │
│ （NVMe 协议在盒子内部终结，存储产品能力，零开发）             │
└──────────────────────────────────────────────────────────┘
```

### 4.3 修改点 A：Host 建链适配（新增模块，改动集中处）

- 新增到存储端点的建链流程：存储 EID + 存储侧 MR token 表导入（存储侧没有 NPU 的 ChannelEntity 结构，token 表通过静态配置或带外通道获取）；
- 产出与现有 `ChannelHandle` 兼容的句柄，引擎层零改动。

### 4.4 修改点 B：device 侧块映射层（新增，数百行）

- `(namespace, LBA)` → 远端地址映射表（建链时由 Host 下发到 GM）；
- 4KB 粒度对齐、大 IO 按 WQE 长度上限分片；
- 命名空间容量与边界检查。

### 4.5 修改点 C：数据面（全部复用现有能力）

- 单块 IO：`ReadNbi`/`WriteNbi`；
- 多块聚合 IO：`MakeBatchHandle` + 批量 `ReadNbi`/`WriteNbi` + `BatchCommit` + 批量 `Drain`——批量路径天然适合聚合多个 LBA 请求，是本方案的性能亮点；
- 写后 flush：按存储产品约定，用 `WriteWithNotifyNbi` 写约定的 flush 地址触发落盘（语义依赖前置确认，见 4.7）。

### 4.6 修改点 D：可靠性包装

- 在 `Drain` 外封装有界超时 + 有限重试（现有 `HCOMM_URMA_MAX_RETRY_TIMES` 为"死等"模型，存储场景需改为有界等待 + 错误码上抛）；
- IO 错误到块层错误码的映射。

### 4.7 约束与依赖

- 依赖 V2/V3：存储产品必须支持 URMA/UB 并以内存语义暴露容量（如 OceanDisk 形态），这是方案成立的唯一硬前提；
- **durability 语义需单独确认**：单边写进存储窗口 ≠ 落盘，必须与存储产品确认持久化触发机制（约定 flush 地址/notify，或控制器自动刷盘）；
- 锁定 UB 存储生态，不能对接通用 NVMe-oF target。

## 5. 两方案对比

| 维度 | 方案一（双边，完整 initiator） | 方案二（单边，target 终结 NVMe） |
| --- | --- | --- |
| 对端要求 | target 必须配合：定制栈或 UB 存储的 capsule 通道 | 存储产品以内存窗口暴露容量 |
| 是否标准协议 | 否（URMA 私有 binding，自定义 capsule 通道） | 否（fabric 上为内存语义，NVMe 在盒子内终结） |
| device 侧新代码 | capsule 层 + IO 热路径（千行级） | 块映射层（数百行） |
| Host 侧新代码 | initiator 协议状态机 + 建链 + MR 导出（大头） | 建链适配 + token 导入（小头） |
| target 侧开发 | 需要配合开发/定制 | 零（依赖存储产品能力） |
| 硬件依赖 | 依赖 V1（SEND/RECV WQE），无则软件模拟双边 | 无额外依赖，现有单边 WQE 够用 |
| 性能 | 数据面单边等价；控制面每条命令多 capsule + notify 开销 | 路径最短；BatchHandle 聚合多块 IO，最贴合引擎设计 |
| Durability 语义 | 完整（NVMe flush 命令语义明确） | 依赖存储产品的窗口持久化约定，需逐一确认 |
| 错误恢复 | 全套自建（abort/重连/队列重建） | 有界重试 + 错误映射，存储内部可靠性由产品兜底 |
| 生态兼容性 | 仍接不了标准 NVMe-oF target | 锁定 UB 存储生态 |
| 开发量/风险 | 大；协议栈 + 双边模拟 + target 配合，三处不确定性 | 中；风险集中在"存储窗口语义"一个外部前提 |

## 6. 选型建议

1. 已有或计划采购 UB 存储（盘即内存形态）→ **方案二**。它是唯一改动与收益成比例的路径，BatchHandle 批量机制与聚合 IO 场景天然匹配。
2. 必须对接自研存储且愿意投入协议栈开发 → 方案一。建议控制面放 Host、只把 IO 热路径留在 device，并优先完成 V1 验证；若 SEND/RECV WQE 存在，方案一可升级为原生双边，性能与语义更干净。
3. 若"必须对接标准 NVMe-oF target"是硬约束 → 两套方案均不成立，需退回 RoCE 网络 + Host 侧标准 initiator（内核 nvme-fabrics / SPDK）的 GDS 式路线，本仓只负责 HBM 内存注册与完成通知。

## 7. 验证方式（实施后）

- 编译验证：单卡环境完成 device 侧新模块编译（参考现有 UT 构建方式，见[构建与测试](../guide/build_and_test.md)）。
- 功能验证：双卡/存储环境端到端读写 + 数据校验（参考 `examples/hcomm_write_read_nbi` 的校验模式）。
- 故障验证：注入超时/断链，验证有界重试与错误上抛。
- 性能验证：聚合 IO 下 BatchHandle 路径的下发时延与带宽（参考 `examples/simt_urma_perftest` 的统计方法）。

## 8. 相关文档

- [Hcomm 使用说明](../guide/hcomm_usage.md)
- [API 参考](../api/README.md)
- [构建与测试](../guide/build_and_test.md)
- [三方依赖与兼容性](../guide/dependencies.md)
