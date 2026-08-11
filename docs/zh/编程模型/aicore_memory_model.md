# AICore（A5）通信内存模型

本文从地址与访问授权视角说明 Ascend 950PR / 950DT（A5）上 AICore 通信的本地与远端内存可见性，覆盖直接使用 `AscendC::Hcomm` 通信原语和使用 `AscendC::Ain` 单边通信接口两种方式。

本文中的“可见”只表示通信引擎已经获得目标内存的地址、有效范围和协议访问凭据，可以通过指定 Channel 发起访问。本文不讨论内存与资源层级、通信操作完成、数据消费顺序、缓存一致性或内存复用时机。

本仓将 AICore 数据面引擎表示为 `COMM_ENGINE_AIV`。HComm 支持 RoCE 和 UBC_CTP/URMA 两条 A5 路径；AIN 当前只将默认通信引擎映射为 UBC_CTP/URMA。

## 四种通信方式的用户视角结论

从用户可见的 API 形态看，可以将四种方式概括为：

- AICPU HComm、AICore HComm 和 CCU 都由用户显式选择 `Channel`，并向该 Channel 提供本地和远端操作数。
- AIN 向用户提供 `Team + peer + Window + offset` 抽象，用户不直接传入 Channel、远端裸地址或 token。
- AIN 并没有用对称窗口取代 Channel 和鉴权。它从 Team 中选择 peer Channel，从 Window 中解析本地和远端地址，再调用 AICore HComm；最终远端访问仍依赖该 Channel 中的授权内存范围和 token。

因此，“前三种是 Channel + 鉴权，AIN 是对称窗口”这句话只对了 API 的第一层形态。更准确的表达是：

> 前三种都向用户显式暴露 Channel；AIN 向用户暴露 Team 和对称窗口。四种方式的远端访问最终都需要“到对端的路径/Channel + 地址范围 + 访问凭据”；AIN 只是把 Channel 选择和远端地址选择封装在 Team/Window 内。

### API 可见性对比

| 通信方式 | 用户提供的本地操作数 | 用户提供的远端操作数 | Channel 对用户是否显式 | 访问凭据对用户是否显式 |
| --- | --- | --- | --- | --- |
| AICPU HComm | 本地裸地址 | 远端裸地址 | 是，原语传入 `ChannelHandle` | 否，`lkey/rkey` 或 UB token 由 Channel 内部保存和使用 |
| AICore HComm | 本地 `GM_ADDR` | 远端 `GM_ADDR` | 是，原语传入 `ChannelHandle` | 否，`lkey/rkey` 或 UB token 由设备侧 `ChannelEntity` 内部查找和使用 |
| CCU | `LocalAddr(addr, token)` | `RemoteAddr(addr, token)` | 是，原语传入 `ChannelHandle` | 是，但 token 是不透明凭据，用户只能查询、传递和组装，不应解析或修改 |
| AIN | `srcWin + self + srcOffset` | `dstWin/srcWin + peer + offset` | 否，由 Team 内部选择 peer Channel | 否，继续使用底层 peer Channel 中的 token |

### 哪些统一，哪些不统一

AICPU HComm 和 AICore HComm 在本文关注的用户契约上基本一致：用户都使用 `ChannelHandle + 本地地址 + 远端地址`，都先通过通信内存注册、Channel 建立和远端描述交换使远端地址可用，都不要求用户在数据面原语中传入 token/key。两者的执行核、原语形式和具体协议实现可以不同，但不改变这一地址与授权可见性契约。

CCU 与两种 HComm 的统一点是，远端操作数仍必须与当前 Channel、远端 rank 和授权内存范围一致。不同点是，CCU 将 token 作为 `LocalAddr/RemoteAddr` 的显式组成部分；因此不能说前三种在“鉴权信息是否对用户隐藏”上完全一致。

AIN 的对称性是逻辑命名对称：各 rank 使用同一窗口语义和 offset 选择对应内存，不要求各 rank 的数值地址相同。窗口元数据仍保存每个 member 的实际 `CommMem.addr`，Team 仍保存到各 peer 的 Channel。

### 统一的使用约束

- “本地可访问”不等于“对端可见”；要对端可见，还必须建立通信访问授权。
- 远端地址只能作为远端通信操作数，不是本端可直接解引用的普通地址。
- 一次访问的地址和长度必须完整落在已授权范围内。
- 远端 rank/peer、Channel、地址、范围和访问凭据必须属于同一访问关系，不能跨 Channel 或跨 peer 混用。

## HComm 的本地与远端地址

### 地址语义

HComm 的公开接口统一使用 `GM_ADDR` 表示本地和远端地址，但两类地址的含义和授权方式不同。

| 地址类型 | 地址来源 | 访问方式 | 访问授权 |
| --- | --- | --- | --- |
| 本地地址 | 本端用户持有的 Device GM 地址 | 可由本端 AICore 直接访问，也可作为 HComm 的本地操作数 | RoCE WQE 使用本地 `lkey`；当前 URMA WQE 的 SGE 携带本地 VA |
| 远端地址 | Channel 建链后查询或交换得到的对端 GM 地址 | 只能作为对应 Channel 上 HComm 原语的远端操作数，不能由本端 AICore 直接解引用 | RoCE 使用远端 `rkey`；UBC_CTP/URMA 使用远端 `tokenId/tokenValue` |

同一个 `GM_ADDR` 类型不会自动表明地址属于本端还是对端，其角色由原语的参数方向决定：

| 原语 | 本地操作数 | 远端操作数 | A5 协议支持 |
| --- | --- | --- | --- |
| [`WriteNbi`](../api/aicore/hcomm/WriteNbi.md) | `src`，本端源地址 | `dst`，对端目的地址 | RoCE、UBC_CTP/URMA |
| [`ReadNbi`](../api/aicore/hcomm/ReadNbi.md) | `dst`，本端目的地址 | `src`，对端源地址 | RoCE、UBC_CTP/URMA |
| `WriteValueNbi` | inline value，不使用本端源地址 | `dst`，对端目的地址 | UBC_CTP/URMA |
| `WriteReduceNbi` | `src`，本端源地址 | `dst`，对端归约目的地址 | UBC_CTP/URMA |
| [`WriteWithNotifyNbi`](../api/aicore/hcomm/WriteWithNotifyNbi.md) | `src`，本端源地址 | `dst` 和 `notifyAddr` | UBC_CTP/URMA |
| [`AtomicFAA`](../api/aicore/hcomm/AtomicFAA.md) / [`AtomicCAS`](../api/aicore/hcomm/AtomicCAS.md) | `fetchAddr`，本端旧值保存地址 | `dst`，对端原子目标地址 | UBC_CTP/URMA |

远端地址只是对端地址空间中的数值，不是本端可直接解引用的虚拟地址。它与远端 rank、Channel、注册内存区域和协议访问凭据绑定，不得用于普通 GM load/store、本地 `DataCopy` 或其他 Channel。

## HComm 远端可见性的建立

直接使用 HComm 时，AICore Kernel 不负责注册或交换通信内存。以仓内 AIV 直驱样例为例，远端可见性在 Host 控制面按以下步骤建立：

1. 用户申请本端 Device 内存，以 `CommMem` 描述地址和大小，并调用 `HcclCommMemReg` 获得通信域内的内存 handle。
2. 将 handle 填入 `HcclChannelDesc.memHandles/memHandleNum`，调用 `HcclChannelAcquire` 创建 `COMM_ENGINE_AIV` Channel。
3. Channel 建链时，通信层注册本端内存并交换地址、大小和协议访问保护信息。
4. Host 调用 `HcclChannelGetRemoteMems` 获取对端地址，并将本端地址、远端地址和设备侧 `ChannelHandle` 下发给 AICore Kernel。
5. 设备侧 `ChannelEntity.localBufferAddr` 和 `remoteBufferAddr` 分别保存本地和远端的 `RegedBufferEntity`。HComm 根据操作地址和长度选择内存表项，并将匹配的协议凭据写入 WQE。

```mermaid
flowchart LR
    MEM["本端 Device 内存"] --> REG["HcclCommMemReg<br/>通信域内 handle"]
    REG --> ACQ["HcclChannelAcquire<br/>COMM_ENGINE_AIV"]
    ACQ --> EXP["注册并交换<br/>地址、范围、访问凭据"]
    EXP --> RMT["对端 Channel 的<br/>remoteBufferAddr"]
    RMT --> OP["远端 HComm 操作数"]
```

本端 AICore 能访问一段 GM，不表示对端通信引擎也能访问。只有完成注册并出现在对端指定 Channel 的 `remoteBufferAddr` 表中的区域，才对该 Channel 的对端通信操作可见。

### Channel 内存表

`ChannelEntity` 分别保存本地和远端注册内存表：

| 表项 | 表示内容 | 使用方式 |
| --- | --- | --- |
| `localBufferAddr/localBufferNum` | 当前 rank 可作为通信本地操作数的内存区域及本地保护信息 | RoCE 根据本地地址和长度选择表项并取得 `lkey` |
| `remoteBufferAddr/remoteBufferNum` | 当前 Channel 对端已交换的内存区域及远端保护信息 | RoCE 取得 `rkey`；UBC_CTP/URMA 取得 `tokenId/tokenValue` |

远端表项不是只有地址和大小的 `CommMem`。它还包含协议访问保护信息，因此只有远端地址而没有匹配的 Channel 表项和凭据，仍然不能建立远端可见性。

## RoCE 与 UBC_CTP/URMA 的授权差异

### RoCE

RoCE 路径对本地与远端操作数分别授权：

1. HComm 在 `remoteBufferAddr` 中查找完整覆盖远端 `[addr, addr + len)` 的表项，取得该表项的 `rkey`。
2. HComm 在 `localBufferAddr` 中查找完整覆盖本地 `[addr, addr + len)` 的表项，取得该表项的 `lkey`。
3. WQE 同时携带远端 VA、`rkey`、本地 VA 和 `lkey`。

因此，RoCE 当前设备侧实现要求本地和远端地址都命中当前 Channel 对应的注册内存表。任一地址越界或不属于对应表项时，HComm 返回失败。

### UBC_CTP/URMA

UBC_CTP/URMA 路径按远端内存表项完成远端授权：

1. HComm 在 `remoteBufferAddr` 中查找完整覆盖远端 `[addr, addr + len)` 的表项。
2. 将表项中的 `tokenId/tokenValue`、远端地址以及 Channel 的远端 EID/TP 信息编码进 SQE。
3. 将本地操作数的 VA 和长度编码进 SGE。

当前 URMA 设备侧代码只对远端操作数调用 `HcommFindBufferIdx`，没有使用 `localBufferAddr` 对本地操作数执行与 RoCE 相同的软件范围检查。这是当前实现的检查差异，不表示任意本地地址都天然具备通信访问能力；调用者仍须传入当前设备和协议支持的有效本端 GM 地址。

### 地址范围匹配

HComm 使用 `HcommFindBufferIdx` 判断操作区间是否完整落在某个注册区域内。若表项范围为 `[base, base + size)`，则操作必须满足：

```text
addr >= base
addr - base <= size
len <= size - (addr - base)
```

远端地址位于某个注册区域内但 `addr + len` 越过区域结尾时，仍不具备该次操作的有效授权。一个操作也不能跨两个相邻的注册表项。

### `WriteWithNotifyNbi` 的通知地址

URMA `WriteWithNotifyNbi` 先根据数据目的地址 `dst` 选择远端内存表项，再使用同一表项的 `tokenId/tokenValue` 编码 `notifyAddr`。当前实现只检查 `dst` 对应的数据区间，没有独立检查 `notifyAddr` 的范围。

因此，`notifyAddr` 必须由 `dst` 命中的同一远端注册内存和 token 保护范围覆盖。不能将数据目的地址和通知地址分别放在使用不同 token 的远端注册区域中。

## AIN 的本地与远端地址

### 对称窗口的地址表示

AIN 是构建在 HComm 之上的单边通信封装。`HcommWindowHandle` 是指向窗口元数据的不透明 GM 句柄；窗口中的 `mems[memberId]` 为每个 team member 保存一个 `CommMem`。AIN 根据 member id 选择地址：

| 地址角色 | 选择方式 |
| --- | --- |
| 本端窗口地址 | `window->mems[team->selfMemberId].addr + offset` |
| 远端窗口地址 | `window->mems[peer].addr + offset` |

“对称”表示各 rank 使用同一个逻辑窗口和 offset 描述对应内存，不要求各 rank 的数值地址相同。A5 URMA 路径仍需在窗口元数据中保存每个 peer 的实际地址。

窗口只负责将 `window + member + offset` 转换为地址，不提供协议访问授权。远端窗口成员能否访问，仍取决于 peer Channel 的 `remoteBufferAddr` 中是否存在覆盖该地址范围的表项，以及表项中是否包含有效的 UB token。

本仓 AIN 代码是设备侧实现，不负责创建 team、注册窗口、交换各 rank 的 `CommMem` 或创建 Channel。调用 AIN 前，这些控制面对象和远端访问凭据必须已经准备完成。

需要注意，hcomm 仓当前公开的 `HcclCommSymWinRegister` 文档和其 `HcclChannelAcquire` 自动追加对称内存 handle 的实现面向 AI CPU 展开场景。不能仅凭名称将该控制面接口直接视为 AIN 的 team/window 创建 API。本文对 AIN 的结论仅基于代码中已经确认的数据面契约：上层提供有效的 Team、Window 和 peer Channel，AIN 完成地址解析并复用 AICore HComm 的授权机制。

### AIN 原语的地址映射

| AIN 原语 | 本地地址 | 远端地址 | 底层授权路径 |
| --- | --- | --- | --- |
| `Put` | `srcWin[self] + srcOffset` | `dstWin[peer] + dstOffset` | peer Channel 上的 HComm `WriteNbi` |
| `PutValue` | 不使用本端 GM 源地址 | `dstWin[peer] + dstOffset` | peer Channel 上的 HComm `WriteValueNbi` |
| `Get` | `dstWin[self] + dstOffset` | `srcWin[peer] + srcOffset` | peer Channel 上的 HComm `ReadNbi` |
| `Signal` | team shadow memory 中的旧值保存地址 | `signalWindow[peer] + signalOffset` | peer Channel 上的 HComm `AtomicFAA` |
| `ReadSignal` / `WaitSignal` | `signalWindow[self] + signalOffset` | 无 | 本端 `ld_dev`，不发起远端访问 |

AIN 不创建一种独立于 HComm 的远端地址空间。`Put`、`Get` 和 `Signal` 解析出本地/远端地址后，最终仍由对应 peer Channel 的 HComm URMA 路径检查远端范围并装载 token。

### AIN 的边界检查

AIN 的 `GetCommMemPtr` 当前只执行 `window->mems[memberId].addr + offset`，不检查：

- `peer` 是否小于 team member 数量；
- `memberId` 是否小于窗口的 `memsNum`；
- offset 和操作长度是否落在 `CommMem.size` 内；
- 地址加法是否溢出。

底层 URMA HComm 会检查解析后的远端地址和长度是否命中 Channel 的远端注册区域，但不会补齐 AIN 的本地窗口范围检查。因此，调用者必须保证本地和远端的 `offset + bytes` 均位于对应窗口内，并确保窗口成员与当前 team、peer Channel 和注册内存表一致。

## HComm 与 AIN 的可见性差异

| 维度 | 直接 HComm | AIN |
| --- | --- | --- |
| 本地/远端地址输入 | 调用方直接传入 `GM_ADDR` | 通过 team、peer、window 和 offset 解析 |
| 远端地址来源 | Host 查询远端内存后下发给 Kernel | 窗口的 `mems[peer]` |
| 远端访问授权 | 当前 Channel 的 `remoteBufferAddr` 及 `rkey`/token | 仍来自底层 peer Channel；窗口本身不提供授权 |
| 协议 | RoCE、UBC_CTP/URMA | 当前默认只支持 UBC_CTP/URMA |
| 远端范围检查 | RoCE 检查本地和远端；URMA 当前只检查远端 | 窗口层不检查 offset/size，解析后依赖 HComm 检查远端范围 |
| 地址的可移植性 | 远端地址绑定查询它的 Channel 和 rank | window handle 可统一表达逻辑窗口，但 `mems[peer]` 中仍是各 rank 的实际地址 |

## 使用约束

- 远端地址必须来自或对应当前 Channel 已交换的远端内存，不能只根据数值地址相同推导远端可见。
- 远端地址、范围、`rkey` 或 token、远端 rank 和 Channel 必须来自同一访问关系，不得跨 Channel 或跨 rank 混用。
- 操作地址和长度必须完整落在一个有效注册区域内，不能跨表项访问。
- RoCE 本地操作数还必须命中当前 Channel 的本地注册内存表；URMA 当前虽未执行同等软件检查，调用者仍需保证本地地址有效且受当前设备与协议支持。
- AIN 对称窗口只提供按 member 和 offset 的地址解析，不替代通信内存注册、远端描述交换或 Channel token 授权。
- AIN 调用方必须自行校验 peer、窗口成员数量、offset、长度和地址溢出，并保证解析出的远端地址属于对应 peer Channel 的远端注册区域。
- `WriteWithNotifyNbi` 的 `notifyAddr` 必须与 `dst` 使用同一远端注册区域和 token。

## 代码依据

- AICPU HComm 原语的 Channel 和本地/远端裸地址参数：[`hcomm/include/hcomm_primitives.h`](../../../../hcomm/include/hcomm_primitives.h)
- CCU 原语的 Channel、`LocalAddr` 和 `RemoteAddr`：[`hcomm/include/ccu/ccu_primitives.hpp`](../../../../hcomm/include/ccu/ccu_primitives.hpp)
- CCU 地址中的显式 token：[`hcomm/include/ccu/ccu_local_addr.hpp`](../../../../hcomm/include/ccu/ccu_local_addr.hpp)、[`hcomm/include/ccu/ccu_remote_addr.hpp`](../../../../hcomm/include/ccu/ccu_remote_addr.hpp)
- CCU Host 侧 token 查询接口：[`hcomm/include/ccu/ccu_res.h`](../../../../hcomm/include/ccu/ccu_res.h)
- HComm 本地/远端参数方向：[`include/aicore/hcomm/hcomm.h`](../../../include/aicore/hcomm/hcomm.h)
- Channel 本地/远端内存表、RoCE key 和 UB token：[`src/aicore/hcomm/detail/common/hcomm_inner_def.h`](../../../src/aicore/hcomm/detail/common/hcomm_inner_def.h)
- 地址范围查找：[`src/aicore/hcomm/detail/common/hcomm_utils.h`](../../../src/aicore/hcomm/detail/common/hcomm_utils.h)
- V310 RoCE 的本地/远端范围检查与 key 装载：[`src/aicore/hcomm/detail/impl/platform_v310/hcomm_aiv_roce.h`](../../../src/aicore/hcomm/detail/impl/platform_v310/hcomm_aiv_roce.h)
- V310 URMA 的远端范围检查、token 和通知地址编码：[`src/aicore/hcomm/detail/impl/platform_v310/hcomm_aiv_urma.h`](../../../src/aicore/hcomm/detail/impl/platform_v310/hcomm_aiv_urma.h)
- AIN 的窗口地址解析和 HComm 映射：[`src/aicore/ain/detail/impl/ain_impl.h`](../../../src/aicore/ain/detail/impl/ain_impl.h)
- AIV 直驱内存注册、Channel 创建与远端地址下发样例：[`examples/hcomm_write_read_nbi/README.md`](../../../examples/hcomm_write_read_nbi/README.md)
- hcomm 对称窗口登记、Channel 建立时的 handle 追加和远端地址回填：[`hcomm/src/coll_communicator_mgr/api_c_adpt/coll_comm_res_c_adpt.cc`](../../../../hcomm/src/coll_communicator_mgr/api_c_adpt/coll_comm_res_c_adpt.cc)
