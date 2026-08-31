# AIV 直驱 URMA 接口冒烟测试设计

> 版本：v0.1　　日期：2026-08-28
> 目标平台：V310 / `COMM_PROTOCOL_UBC_CTP`（AIV 直驱 URMA）
> 关联头文件：`include/aicore/hcomm/hcomm.h`、`src/aicore/hcomm/impl/platform_v310/hcomm_aiv_urma*.h`

## 1. 背景与目标

维护 AIV 直驱 URMA 接口的功能，需要一套可长期回归的冒烟用例。本设计把待覆盖的测试内容按维度归并到最少数量的 kernel 中，每个 kernel 只描述**被测接口的调用序列（伪码）**，不涉及 kernel 内 tiling / 同步 / 校验框架等细节。校验逻辑（数据比对、返回码判断）由 host 侧或 kernel 尾部统一处理，此处省略。

伪码变量约定（对应实际 kernel 中的声明，见示例 `examples/hcomm_write_read_nbi/hcomm_write_read_nbi_kernel.cpp`）：

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;   // Hcomm 实例（URMA 协议）

AscendC::TPipe pipe;
AscendC::TBuf<AscendC::TPosition::VECIN> workspaceBuf;  // 给 Init 用的 UB 工作区
pipe.InitBuffer(workspaceBuf, WORKSPACE_SIZE);          // WORKSPACE_SIZE：工作区字节数，取 512（>= HCOMM_URMA_TMP_BUF_SIZE）
AscendC::LocalTensor<uint8_t> workspaceTensor = workspaceBuf.Get<uint8_t>();  // Init 的第 1 个入参

AscendC::ChannelHandle channel   = context->channelHandle;                     // 通信通道句柄（host 建链后通过 context 传入）
GM_ADDR remoteBase = reinterpret_cast<GM_ADDR>(context->remoteBufferAddr);      // 远端已注册内存基址（写入目标）
GM_ADDR localBase  = reinterpret_cast<GM_ADDR>(context->localBufferAddr);       // 本地已注册内存基址（本卡的接收/源缓冲）
```

因此下文伪码里：
- `hcomm` = `AscendC::Hcomm<COMM_PROTOCOL_UBC_CTP>` 实例。
- `hcomm.Init(workspaceTensor, WORKSPACE_SIZE)`：第 1 个入参是 UB 上的工作区 `LocalTensor<uint8_t>`，第 2 个入参是该工作区字节数（取 512）。
- `channel` = `ChannelHandle`（原缩写 `ch`）。
- `remoteBase` / `localBase` = 远端 / 本地已注册内存基址（原缩写 `rAddr` / `lAddr`）。
- `sqDepth` 由控制面（host 建链参数）决定，kernel 内不可改。

### 多卡寻址约定（重要，避免落点冲突）
注册内存是**收发共享**的：当卡 S 往一个 peer 写时，数据落在**该 peer 的注册内存**里——也就是那个 peer 眼中的 `localBase`，同时是 S 眼中的 `remoteBase`。因此如果所有卡都用相同偏移（尤其偏移 0）去写，多个 sender 的数据会在同一个接收方 `localBase` 上互相覆盖，接收方本地校验就会读到被别人踩过的数据。

约定：**写入目标偏移按“发送方 rank”分槽**，让不同 sender 的写落在接收方缓冲里互不重叠的槽位；本卡自己的"发送源 / Read 读回 / atomic fetch 回写"放在**谁都不会写入的私有区**，与接收区分离；整体避开偏移 0（留给 barrier/flag）。

```cpp
uint32_t myRank = context->rank;                 // 本卡 rank id（host 通过 context 传入）
uint32_t peerRank = 1 - myRank;                  // 2P 场景对端 rank（多卡则需遍历所有 sender）
constexpr uint32_t RANK_STRIDE = 8 * 1024;       // 每个发送方在接收方缓冲里独占的槽大小（按单卡单轮最大落点取，示例 8KB）
constexpr uint32_t RECV_BASE   = 4 * 1024;       // 接收区起点，跳过偏移 0 的公共区（barrier/flag）
constexpr uint32_t SCRATCH_BASE = RECV_BASE + MAX_RANKS * RANK_STRIDE;  // 私有区起点，排在所有接收槽之后

// 【写目标 dstSlot，在对端注册内存里 → 本卡写得到但读不到，不能自校验】
// 按“本卡 rank”选槽 → 落在对端缓冲 [RECV_BASE + myRank*STRIDE, ...)，与其他 sender 不冲突
GM_ADDR dstSlot = remoteBase + RECV_BASE + myRank * RANK_STRIDE;

// 【接收槽 recvSlot，在本卡自己的 localBase 里 → 对端按它的 sender rank 写进来，本卡能读、能校验】
// 2P：对端 = peerRank，它写进我的缓冲落在 RECV_BASE + peerRank*STRIDE
GM_ADDR recvSlot = localBase + RECV_BASE + peerRank * RANK_STRIDE;

// 本卡私有区起点：每个 kernel 在此基础上按自己的排布切三片（发送源 / Read 读回 / fetch 回写）
GM_ADDR scratch = localBase + SCRATCH_BASE;
```

**校验可见性原则（真机约束，务必遵守）：**
- 本卡**只能读自己 `localBase` 范围内的数据**（`recvSlot` 段 + 本卡私有 scratch 三片）；写出去的 `dstSlot` 在对端内存里，本卡读不到，**不能用来自校验**。
- 因此：**写类接口（Write/WriteValue/WriteReduce/WriteWithNotify）发出的数据由对端校验**——对端在它的 `recvSlot` 里比对。两卡跑相同代码，天然对称：我发的对端校、对端发的我校。
- **ReadNbi 读回的数据落在本地读回片**，本卡可直接校验。
- **Atomic 旧值回写到本地 fetch 片**，本卡可直接校验；atomic 在对端产生的新值由对端在其 `recvSlot` 校验。

**每个 kernel 的本地 scratch 三片排布各自独立**（各 kernel 是独立用例，互不依赖），在各 kernel 的“数据存放方式”小节里定义。通用约束：`RANK_STRIDE` 容纳单卡单轮最大落点；接收区 + 私有区总和不超过注册内存大小；写类校验一律由对端读其本地 `recvSlot`。

## 2. 被测接口清单

### 2.1 控制 / 生命周期
- `Init(localTensor, len)` / `Init(uint8_t* buff, len)`
- `Commit<pipe>(channel)`
- `Drain<pipe>(channel)`
- `Lock(channel)` / `Unlock(channel)`（V310 支持，本仓库；同设备多 AI Core 共享同一通道的互斥，见 K8）

### 2.2 P2P 任务接口（ChannelHandle）
- `WriteNbi<commit,...,config>(channel, dst, src, len)`
- `WriteValueNbi<T,commit,...,config>(channel, dst, value)`（inline 写）
- `WriteReduceNbi<T,reduceOp,commit,...,config>(channel, dst, src, count)`
- `WriteWithNotifyNbi<commit,...,config>(channel, dst, src, len, notifyAddr, notifyVal)`
- `AtomicFAA<T,commit,...,config>(channel, dst, fetchAddr, addVal)`
- `AtomicCAS<T,commit,...,config>(channel, dst, fetchAddr, compareVal, swapVal)`
- `ReadNbi<commit,...,config>(channel, dst, src, len)`

### 2.3 Batch 接口（BatchHandle / MultiChannelHandle）
- `MakeBatchHandle(channel 或 multiChannel, buff, buffLen, remoteAddr, localAddr)`
- `GetHandleRef(batchHandle, channelIndex, remoteAddr)`
- `WriteNbi<config>(batchHandle, dst, src, len)`
- `WriteWithNotifyNbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal)`
- `ReadNbi<config>(batchHandle, dst, src, len)`
- `BatchCommit(batchHandle)`
- `Drain(batchHandle)`

### 2.4 配置（`UrmaWqeEntry`，`config` 模板参）
字段：`odr`(3bit, ordering) / `fence`(1) / `se`(1) / `cqe`(1, 是否产生 CQE) / `inlineEn`(1)。
- `URMA_DEFAULT_CFG` = {odr=5, fence=1, se=0, cqe=1, inlineEn=0}
- `URMA_INLINE_CFG`  = {odr=5, fence=1, se=0, cqe=1, inlineEn=1}
- **`odr` 语义（以此为准）：`odr=5` 为 relax order，`odr=6` 为 strong order。**
  头文件 `hcomm_inner_def.h:40` 注释写的 “5=strongly ordered” 有误，测试以本表为准。
- 本设计额外用到的自定义 config：
  - `CFG_RELAX_CQE`    = {odr=5, fence=1, cqe=1}  （relax order，产生 CQE）
  - `CFG_STRONG_CQE`   = {odr=6, fence=1, cqe=1}  （strong order，产生 CQE）
  - `CFG_RELAX_NOCQE`  = {odr=5, fence=1, cqe=0}  （relax order，不产生 CQE，用于延迟提交的中间任务 / SQ 安全模式回收）

### 2.5 接口使用约束（用例编写须遵守）
从 `docs/zh/api/aicore/hcomm/*.md` 与实现（`hcomm_aiv_urma.h`）提炼，直接约束用例结构：
- **Init**：workspace `len ∈ [512, buff.GetSize()]` 字节；仅普通 `ChannelHandle` 链需要，Batch 链不需要 Init。
- **commit=false 连续调用次数不得超过 sqDepth**，否则后续 PostSend 因 SQ 溢出失败。→ 延迟提交循环上限必须 `≤ sqDepth`（用例取 `sqDepth-1` 留余量）。
- **延迟提交批处理**：仅最后一次提交的任务 `cqe=1`，中间任务 `cqe=0`（配合 fence/order 保证末次在前序之后完成，用末次 CQE 代表整批完成）。
- **WriteWithNotifyNbi 占 2 个 WQEBB**（普通与 batch 均是），压测/延迟提交计数按 2 倍消耗 SQ。
- **WriteReduceNbi**：`T ∈ {int8_t,int16_t,int32_t,uint32_t,half,float,bfloat16_t}`，`reduceOp ∈ {MAX,MIN,SUM}`，`config.inlineEn` 必须为 0。
- **WriteValueNbi**：必须 `config.inlineEn=1`（用 `URMA_INLINE_CFG`）。
- **AtomicFAA / AtomicCAS**：`T ∈ {int32_t,uint32_t,int64_t,uint64_t}`。
- **dst 必须落在通道注册的远端 buffer 范围内**；Atomic 的 `fetchAddr`、Notify 的 `notifyAddr` 复用同一注册内存 token，须在同一注册区间内。
- **普通 Commit 与 BatchCommit 不能在同一通道混用**（队列状态管理方式不同）。
- **全 `cqe=0` 时 Drain 直接返回成功，但不代表硬件已完成**（docs/Drain 明确）；因此 K4 的“不产生 CQE”组合不能用 Drain 返回值断言完成，需靠远端数据/notify 或末次 `cqe=1` 兜底。
- **Batch 接口**：`buff` 32 字节对齐；`64 ≤ buffLen < sqDepth*64`；可备 WQEBB 数 = `buffLen/64`。BatchHandle 有效期需独占通道，不得与普通接口交叉。
- **Lock/Unlock**：V310 已支持（本仓库）；锁不可重入，成功 Lock 后必须由同一 AI Core Unlock，异常路径也要释放。

## 3. 测试维度 → kernel 映射

| 维度 | 内容 | 归属 kernel |
|------|------|-------------|
| 1 | 所有接口普通功能 | **K1**（P2P）+ **K2**（Batch） |
| 2 | 各接口压测 + 循环超过 sqDepth | **K3** |
| 3 | sqDepth × commit 延迟 × 是否产生 CQE 组合 | **K4**（host 参数驱动） |
| 4 | 多次 kernel launch 循环 | host 侧循环，见 §6，不单独建 kernel |
| 5 | 保序功能（relax + 末次 strong order=6） | **K5** |
| 6 | 普通接口性能（warmup + 循环取平均） | **K6** |
| 7 | batch 接口性能 | **K7** |
| 8 | Lock/Unlock 多核互斥 | **K8**（多 AI Core 并发同一 channel） |

共 **8 个 kernel**（K1、K2 均为“功能”类，因 handle 体系不同而分开；K8 因需多核并发而独立，不能并入单序列的 K1）。

## 4. 各 kernel 详细设计（伪码）

### K1 — P2P 全接口功能冒烟（`kernel_urma_p2p_func`）
目的：每个 P2P 接口各跑一次正常路径，返回码为 0 且对端数据/本地回读值正确。用不同偏移隔离各接口落点，最后一把 Drain 收尾。

#### K1.1 数据存放方式
本卡私有 scratch 切三片。**发送源只用一片 256B**：视为 64×int32，每个元素初值 `10 + myRank`（可辨识、跨卡可区分）。所有写类接口都从 `srcBuf + 0` 取，长度 256B（reduce 即 64×int32）。

```cpp
// —— 片1：发送源 srcBuf —— 一片 256B，64×int32，每个 = 10 + myRank
GM_ADDR srcBuf   = scratch;              // 所有 Write/WriteReduce/WriteWithNotify 都从 srcBuf+0 取 256B
uint32_t sendVal = 10 + myRank;          // WriteValueNbi 的 inline 值，也用 10+myRank，语义一致
// —— 片2：Read 读回落点 readBuf ——
GM_ADDR readBuf  = scratch + 4 * 1024;   // ReadNbi 本地 dst
// —— 片3：atomic fetch 回写落点 fetchBuf ——
GM_ADDR fetchBuf = scratch + 8 * 1024;   // +0: FAA 旧值; +8: CAS 旧值

// 写目标：本卡按 myRank 分槽写到对端 dstSlot，各接口 dst 段内偏移错开；
// notify 落点直接放本次数据之后一格（同槽同注册内存 token）。
GM_ADDR notifyDst = dstSlot + 1280 + 256;   // = dstSlot + 1536
```

**初值要求**：
- 源 `srcBuf`：64×int32 全填 `10 + myRank`（host 或 kernel 头部）。
- 本卡**接收区 `recvSlot`**（对端来写、本卡校验）：普通写 / inline / notify 数据格 + notify flag 清 `0`；SUM reduce 格清 `0`；MAX reduce 格填 `INT32_MIN`；FAA/CAS 格清 `0`。
- 本卡 `fetchBuf` 清 `0`。
- dst 段内偏移无重叠：write(256)/wValue(512)/SUM(768)/MAX(1024)/notify(1280..1536)+flag(1536)/FAA(2304)/CAS(2368)，每格 256B（atomic 8B）。

> 注：本片全为 int32，故 reduce 覆盖 **int32 SUM + int32 MAX**。若还需覆盖 float reduce，另留一小片 float src（不在这一片内）单独测，见 §7 open item。

#### K1.2 测试接口伪码
```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);

// write：从 srcBuf+0 写 256 字节
hcomm.WriteNbi(channel, dstSlot + 256,  srcBuf + 0, 256);
// write value：inline 单值写，值 = 10+myRank
hcomm.WriteValueNbi<uint32_t>(channel, dstSlot + 512, sendVal);
// write reduce：对端做 sum/max，同一片 int32 源（覆盖 SUM + MAX）
hcomm.WriteReduceNbi<int32_t, HcommUrmaReduceOp::SUM>(channel, dstSlot + 768,  srcBuf + 0, 64 /*count*/);
hcomm.WriteReduceNbi<int32_t, HcommUrmaReduceOp::MAX>(channel, dstSlot + 1024, srcBuf + 0, 64 /*count*/);
// write with notify：从 srcBuf+0 写 256B + 敲对端 notify flag
hcomm.WriteWithNotifyNbi(channel, dstSlot + 1280, srcBuf + 0, 256, notifyDst, 0x1U);
// atomic FAA：对端 += addVal，旧值回本卡 fetchBuf
hcomm.AtomicFAA<uint64_t>(channel, dstSlot + 2304, fetchBuf + 0, 10);
// atomic CAS：对端 == cmp 则写 swap，旧值回 fetchBuf
hcomm.AtomicCAS<uint64_t>(channel, dstSlot + 2368, fetchBuf + 8, /*cmp*/0, /*swap*/0x77);
// read：从对端本卡槽读回本卡 readBuf（读回自己刚写进对端的那一格）
hcomm.ReadNbi(channel, readBuf + 0, dstSlot + 256, 256);

hcomm.Drain(channel);   // 阻塞回收全部 CQE
```

#### K1.3 精度校验
**一律读本地**（两卡代码相同，各校各的本地）：`recvSlot` 里是对端写进来的数据，预期 = 对端源值 `10 + peerRank`；`readBuf`/`fetchBuf` 是本卡自己操作的结果。

| 接口 | 校验地址（本地） | 预期数据 |
|------|-----------------|---------|
| WriteNbi | `recvSlot + 256`，64×int32 | 每个 == `10 + peerRank` |
| WriteValueNbi | `recvSlot + 512`，1×int32 | == `10 + peerRank` |
| WriteReduceNbi<int32,SUM> | `recvSlot + 768`，64×int32 | 每个 == `0 + (10+peerRank)` = `10 + peerRank`（初值 0） |
| WriteReduceNbi<int32,MAX> | `recvSlot + 1024`，64×int32 | 每个 == `max(INT32_MIN, 10+peerRank)` = `10 + peerRank`（初值 INT32_MIN） |
| WriteWithNotifyNbi | `recvSlot + 1280`（64×int32）+ `recvSlot + 1536`（flag） | 数据每个 == `10 + peerRank`；flag == `0x1` |
| AtomicFAA | `fetchBuf + 0`（本卡旧值回写） | == `0`（对端该地址在本卡 FAA 前的初值） |
| AtomicCAS | `fetchBuf + 8`（本卡旧值回写） | == `0`（对端该地址旧值，cmp 0 命中） |
| ReadNbi | `readBuf + 0`，64×int32 | 每个 == `10 + myRank`（读回本卡自己写进对端、再读回来的值） |

> atomic 的“对端新值”落在对端内存，本卡读不到，故本卡只校验本地 `fetchBuf` 旧值；对端会在它自己的 `recvSlot` 上校验它收到的 atomic 新值（两卡对称）。

> ReadNbi 读的是对端 `dstSlot+256`，那里是本卡自己 WriteNbi 写进去的数据，故本地 `readBuf` 预期 == 本卡 `srcBuf + 0`，本卡即可自校验。

### K2 — Batch 全接口功能冒烟（`kernel_urma_batch_func`）【@@@吴治锋】
目的：覆盖 batch 侧 Write / WriteWithNotify / Read + BatchCommit + Drain。单 channel 用 `ChannelHandle`，多 channel 走 `MultiChannelHandle` + `GetHandleRef`。

#### K2.1 数据存放方式
本 kernel 只用两片本地缓冲（无 atomic）：

```cpp
GM_ADDR srcBuf  = scratch;              // 发送源：+0 / +256 各 256B（64×int32），全填 10 + myRank
GM_ADDR readBuf = scratch + 4 * 1024;   // ReadNbi 读回落点
GM_ADDR notifyDst = dstSlot + 256 + 256;// = dstSlot + 512，notify 落点紧跟数据（同槽同 token）
```
**初值要求**：源 `srcBuf` 两段填 `10 + myRank`；本卡接收区 `recvSlot + 0`（write）、`recvSlot + 256`（notify 数据）、`recvSlot + 512`（notify flag）清 `0`。多 channel 时本卡对每个 peer 的接收区同样清 0。

#### K2.2 测试接口伪码
```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);                    // Batch Drain 不强依赖 Init，功能用例统一初始化
auto batchHandle = hcomm.MakeBatchHandle(channel, batchTensor, BATCH_LEN, remoteBase, localBase);

hcomm.WriteNbi(batchHandle, dstSlot + 0,   srcBuf + 0, 256);
hcomm.WriteWithNotifyNbi(batchHandle, dstSlot + 256, srcBuf + 256, 256, notifyDst, 0x1U);
hcomm.ReadNbi(batchHandle, readBuf + 0, dstSlot + 0, 256);

hcomm.BatchCommit(batchHandle);   // 一次性提交上面所有 WQE
hcomm.Drain(batchHandle);         // batchHandle 的 WQE buffer 复用为 CQE scratch
```

多 channel（MultiChannelHandle）分支【@@@卢贤斌】：
```cpp
auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchTensor, BATCH_LEN);
for (i = 0; i < channelNum; ++i) {
    auto& peerHandle = hcomm.GetHandleRef(multiBatchHandle, i, remoteAddr[i]);
    hcomm.WriteNbi(peerHandle, remoteAddr[i], srcBuf + 0, 256);
}
hcomm.BatchCommit(multiBatchHandle);
hcomm.Drain(multiBatchHandle);
```

#### K2.3 精度校验
一律读本地：`recvSlot` 是对端写进来的（预期 = `10 + peerRank`），`readBuf` 是本卡读回自己写出的（预期 = `10 + myRank`）。

| 接口 | 校验地址（本地） | 预期数据 |
|------|-----------------|---------|
| WriteNbi(batch) | `recvSlot + 0`，64×int32 | 每个 == `10 + peerRank` |
| WriteWithNotifyNbi(batch) | `recvSlot + 256`（64×int32）+ `recvSlot + 512`（flag） | 数据每个 == `10 + peerRank`；flag == `0x1` |
| ReadNbi(batch) | `readBuf + 0`，64×int32 | 每个 == `10 + myRank`（读回本卡自己写进对端、再读回的值） |
| WriteNbi(peer i，多 channel) | 本卡对应 peer i 的 `recvSlot` | 每个 == `10 + peer_i_rank` |

### K3 — 压测 + 循环超过 sqDepth（`kernel_urma_stress`）
目的：连续提交次数 `N > sqDepth`，验证 SQ 满时内部溢出回收（`PollCqWhenSqOverflow`）能自动 poll CQ 腾出空间、不死锁、数据不丢。建议 `N = sqDepth * 4 + 3`（跨多圈且非整除）。**主压 WriteNbi + ReadNbi 两种**（数据面最常用、最能压 SQ/CQ 回卷）；均 `commit=true`（每次自带提交），不受“commit=false ≤ sqDepth”约束。

#### K3.1 数据存放方式
```cpp
GM_ADDR srcBuf  = scratch;              // 发送源：256B，64×int32，全填 10 + myRank
GM_ADDR readBuf = scratch + 4 * 1024;   // ReadNbi 读回落点，按 SLOTS 分格
constexpr uint32_t SLOTS = 8;           // 落点轮转格数（≤ sqDepth），避免自覆盖
```
**初值要求**：本卡接收区 `recvSlot + s*256`（s=0..SLOTS-1，write 轮转落点）清 `0`。本 kernel 无 atomic，不需 fetch 片。

#### K3.2 测试接口伪码
```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);

// 用最小 sqDepth（host 配 16）放大回卷压力；每次 commit=true 立即提交
// —— WriteNbi 压测 ——
for (i = 0; i < N; ++i)
    hcomm.WriteNbi(channel, dstSlot + (i % SLOTS) * 256, srcBuf, 256);   // 落点轮转避免自覆盖
hcomm.Drain(channel);

// —— ReadNbi 压测 ——
for (i = 0; i < N; ++i)
    hcomm.ReadNbi(channel, readBuf + (i % SLOTS) * 256, dstSlot, 256);
hcomm.Drain(channel);
```

#### K3.3 精度校验
压测重点是"不丢不错不死锁"，按最终态校验；一律读本地（`recvSlot` 是对端写进来的，`readBuf` 是本卡读回结果）。

| 轮次 | 校验地址（本地） | 预期数据 |
|------|-----------------|---------|
| WriteNbi × N | `recvSlot + s*256`，s=0..SLOTS-1 | 每 slot == 对端源 `10 + peerRank`（对端每次都写同一 src） |
| ReadNbi × N | `readBuf + s*256`，s=0..SLOTS-1 | 每个 == 本卡从对端读回的当前内容（读回一致即可） |

> 全部返回码为 0，且无 PollCq 超时（`0xFFU`）即通过。SLOTS 建议取 8 或 16。其余接口（WriteValueNbi/WriteReduceNbi/WriteWithNotifyNbi/Atomic）的功能正确性由 K1 覆盖，压测不再逐个重复。

### K4 — 控制面组合测试（`kernel_urma_matrix`）
目的：`sqDepth × commit策略 × 是否产生CQE` 三维组合。sqDepth 由 host 建链参数注入；commit 策略与 cqe 由 kernel 模板参 / 分支选择。host 侧对每个 sqDepth 各拉起一次本 kernel。

- **sqDepth 取值（host 注入，取值范围 [16, 8192] 的 2 的幂）**：建议 `{16, 64, 512, 8192}`（最小、常用、较大、最大各一档）。
- commit 策略（3 种）：
  - A 立即提交：`commit=true`，每次自带提交
  - B 延迟统一提交：连续 `commit=false` × `(sqDepth-1)` 次 + 末尾 `Commit(channel)`（**不得超过 sqDepth**）
  - C 混用：分组，每组内前若干次 `commit=false`、组尾一次 `commit=true`
- CQE：`cqe=1`（产生）/ 延迟提交时中间 `cqe=0`、末次 `cqe=1`

> 约束提醒：commit=false 连续次数必须 ≤ sqDepth；WriteWithNotify 占 2 WQEBB，此处用普通 Write（1 WQEBB）计数。SLOTS 取 ≤ sqDepth 的落点数避免自覆盖。

#### K4.1 数据存放方式
```cpp
GM_ADDR srcBuf = scratch;               // 发送源：256B，64×int32，全填 10 + myRank
constexpr uint32_t SLOTS = 8;           // 落点轮转格数（≤ sqDepth）
```
**初值要求**：本卡接收区 `recvSlot + (i%SLOTS)*256` 各格清 `0`。每个 sqDepth 档位、每组 A/B/C 跑之前都重清一次，避免上一组残留干扰判定。本 kernel 无 Read / atomic，故不需读回 / fetch 片。

#### K4.2 测试接口伪码
```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);
constexpr uint32_t M = sqDepth - 1;   // 延迟提交上限：严格小于 sqDepth，避免 SQ 溢出

// —— A: 立即提交 × 产生 CQE ——（每次都提交，可跨多圈，验证内部溢出回收）
for (i=0; i<sqDepth*4+3; ++i)
    hcomm.WriteNbi<true, PIPE_S, PIPE_MTE3, CFG_RELAX_CQE>(channel, dstSlot+(i%SLOTS)*256, srcBuf, 256);
hcomm.Drain(channel);

// —— B: 延迟提交（commit=false）× 中间不产生 CQE，末次产生 CQE ——
// 外层再套 ROUNDS 次循环：验证「延迟提交批处理」整套流程可重复、每轮 Commit+Drain 后 SQ/CQ 状态正确复位
constexpr uint32_t ROUNDS = 4;
for (r = 0; r < ROUNDS; ++r) {
    for (i=0; i<M-1; ++i)
        hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, CFG_RELAX_NOCQE>(channel, dstSlot+(i%SLOTS)*256, srcBuf, 256); // cqe=0
    hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, CFG_STRONG_CQE>(channel, dstSlot+((M-1)%SLOTS)*256, srcBuf, 256);   // 末次 cqe=1
    hcomm.Commit(channel);   // 统一提交这一批
    hcomm.Drain(channel);    // 末次 strong+cqe=1 到位 ⇒ 整批到位；本轮结束后 SQ/CQ 应复位
}

// —— C: 分组混用（组尾 commit=true）× 全程产生 CQE ——
constexpr uint32_t GROUP_SZ = sqDepth / 2;   // 每组任务数（≤ sqDepth，留余量）；组内前 GROUP_SZ-1 延迟、组尾立即提交
constexpr uint32_t GROUPS   = 4;             // 组数：多组验证组间状态复位
for (g=0; g<GROUPS; ++g) {
    for (j=0; j<GROUP_SZ-1; ++j)
        hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, CFG_RELAX_CQE>(channel, dstSlot+(j%SLOTS)*256, srcBuf, 256);
    hcomm.WriteNbi<true, PIPE_S, PIPE_MTE3, CFG_RELAX_CQE>(channel, dstSlot+((GROUP_SZ-1)%SLOTS)*256, srcBuf, 256);
}
hcomm.Drain(channel);
```

#### K4.3 精度校验
组合矩阵（host 遍历 4 档 sqDepth，kernel 内遍历 A/B/C）：A 立即提交跨圈压回收；B 延迟提交 + 中间 cqe=0/末次 cqe=1；C 分组混用。不产生 CQE 的组合重点观察 SQ 安全模式停止条件是否会空转到超时（关联 PollCqWhenSqOverflow）。一律读本地 `recvSlot`（对端写进来，预期 = 对端源 `10 + peerRank`）。

| 组 | 校验地址（本地） | 预期数据 |
|----|-----------------|---------|
| A（立即提交，跨圈） | `recvSlot + s*256`，s=0..SLOTS-1 | 每 slot == `10 + peerRank`；返回码全 0、无超时 |
| B（延迟提交，中间 cqe=0/末次 cqe=1，重复 ROUNDS 轮） | `recvSlot + (i%SLOTS)*256`, i=0..M-1 | 每轮全部 M 个写到位（值 == `10 + peerRank`；末次 strong+cqe=1 到位 ⇒ 前序全到位）；**不可用 Drain 返回值判中间完成**；每轮结束 SQ/CQ 应复位、下一轮仍正确 |
| C（分组混用） | 各 `recvSlot` slot | 每组内全部到位（== `10 + peerRank`）；返回码全 0 |

> 每个 sqDepth 档位（16/64/512/8192）各跑一遍 A/B/C，结果一致才算通过。

### K5 — 保序功能测试（`kernel_urma_order`）
目的：前面若干次用 relax order（odr=5）写不同地址、**均不产生 CQE**（cqe=0），最后一次对“哨兵格”用 strong order（odr=6）写、**只有它产生 CQE**（cqe=1）。校验规则：**只要最后一次 strong-order 数据到位，则前面所有 relax 写也必须已到位**（strong order 语义保证全局可见顺序）。用“只 poll 到末次 strong 的 CQE，就断言前序全到位”正好压中这个语义。

#### K5.1 数据存放方式
```cpp
constexpr uint32_t BLK = 1024;          // 前序 relax 每次写的大块，1KB（比普通 256B 大，放大乱序窗口）
constexpr uint32_t K   = 6;             // relax 写次数；约束 K*BLK + 4（哨兵）≤ RANK_STRIDE(8KB) → 6KB+4 OK
GM_ADDR srcBuf = scratch;               // 发送源：+i*BLK（i=0..K-1）各 1KB，pattern 0x5EED0000|i（便于定位缺失段）
GM_ADDR sentinelSrc = srcBuf + K * BLK; // 哨兵源：srcBuf 里预留一格 4B，填 0x5A5A5A5A（同为已注册区，可直接作 src）
GM_ADDR sentinelDst = dstSlot + K * BLK;// 哨兵落点：排在前 K 个大块之后（= dstSlot + 6144）
```
**初值要求**：源 `srcBuf` 各大块填 `0x5EED0000|i`，`sentinelSrc` 处填 `0x5A5A5A5A`；本卡接收区 `recvSlot + i*BLK`（i=0..K-1，各 1KB）+ `recvSlot + K*BLK`（哨兵格 4B）全部清 `0`。本 kernel 无 Read / atomic。

#### K5.2 测试接口伪码
```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);

// 前 K 次：relax order（odr=5）+ 不产生 CQE（cqe=0），写大块数据
for (i = 0; i < K; ++i)
    hcomm.WriteNbi<true, PIPE_S, PIPE_MTE3, CFG_RELAX_NOCQE>(channel, dstSlot + i*BLK, srcBuf + i*BLK, BLK);
// 末次：strong order（odr=6）+ 产生 CQE（cqe=1），只写哨兵单个数字（4 字节 int32，源在 srcBuf 预留格）
hcomm.WriteNbi<true, PIPE_S, PIPE_MTE3, CFG_STRONG_CQE>(channel, sentinelDst, sentinelSrc, 4);

hcomm.Drain(channel);   // 只有末次 strong 有 CQE → Drain poll 到它即返回；此刻前序 relax 必须已全部到位
```

#### K5.3 精度校验
保序是**接收方视角**的可见性顺序，本卡在自己的 `recvSlot`（对端写进来）上判定。K5 源每段用可区分 pattern `0x5EED0000|i`（两卡相同，便于定位缺失段）：

| 步骤 | 校验地址（本地） | 预期数据 |
|------|-----------------|---------|
| 1. 先判哨兵 | `recvSlot + K*BLK`，4B | == `0x5A5A5A5A` |
| 2. 哨兵到位后判前序 | `recvSlot + i*BLK`，i=0..K-1，各 1KB | 每个 == `0x5EED0000 \| i` |

判定：**本卡观察到哨兵到位（对端 strong-order + 唯一 CQE 的写已完成）时，前 K 个 relax（cqe=0）写必须全部到位**；只要有一个前序缺失即判失败（说明 strong order 未如预期兜住前序）。建议 `K = 6`、`BLK = 1KB`。

### K6 — 普通接口性能测试（`kernel_urma_perf_p2p`）
目的：测 **WriteNbi / ReadNbi / WriteWithNotifyNbi 三种接口**的性能，每种接口用**两种打点方式**：
- **方式 A（不带 Drain）**：只测「发起/提交」开销——打点区间内连续发 `ITERS` 次，末尾不 Drain 就停表（不关心数据是否真到、也不关心数据量）。反映纯提交路径时延。
- **方式 B（带 Drain）**：打点区间内发 `ITERS` 次后 Drain 等全部完成再停表——反映端到端（含真正传输 + CQE 回收）。**扫多种数据量，单次最大 256M**。

warmup 预热排除建链/首包抖动。建议 `WARMUP = 20`；方式 A `ITERS = 1000`；方式 B `ITERS` 随数据量调（大包少循环，小包多循环）。打点用 `AscendC::PipeBarrier<PIPE_ALL>()` + `AscendC::GetSystemCycle()`。

#### K6.1 数据存放方式
```cpp
GM_ADDR srcBuf = scratch;   // 发送源：最大 MSG_LEN（256M）字节，任意 pattern（性能 kernel 不校验数据内容）
// 数据量扫描（方式 B）：MSG_LEN ∈ {256, 4K, 64K, 1M, 16M, 256M}；单次最大 256M
// 方式 A 不关心数据量，固定用一个小包（如 256B）即可
```
**初值要求**：无需特定初值；对端落点不比对。注意 `srcBuf` 与对端 `dstSlot` 落点都要能容纳最大 256M（受注册内存大小限制，若注册内存 < 256M 则上限相应下调，见 §7）。

#### K6.2 测试接口伪码
三种接口 × 两种方式，结构相同，下面以 WriteNbi 为例；ReadNbi / WriteWithNotifyNbi 替换被测调用即可。

```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);

// ===== 方式 A：不带 Drain（只测提交开销，不管数据量/是否到达）=====
for (i = 0; i < WARMUP; ++i) hcomm.WriteNbi(channel, dstSlot, srcBuf, 256);
hcomm.Drain(channel);                                  // warmup 后清空，保证计时段起点干净
AscendC::PipeBarrier<PIPE_ALL>();
uint64_t a0 = AscendC::GetSystemCycle();
for (i = 0; i < ITERS; ++i) hcomm.WriteNbi(channel, dstSlot, srcBuf, 256);
AscendC::PipeBarrier<PIPE_ALL>();
uint64_t a1 = AscendC::GetSystemCycle();
// submit_avg = (a1 - a0) / ITERS  （不含传输完成）
hcomm.Drain(channel);                                  // 计时结束后再收尾，回收前面发出去的任务

// ===== 方式 B：带 Drain（端到端，扫数据量，单次最大 256M）=====
for (each MSG_LEN in {256, 4K, 64K, 1M, 16M, 256M}) {
    for (i = 0; i < WARMUP; ++i) hcomm.WriteNbi(channel, dstSlot, srcBuf, MSG_LEN);
    hcomm.Drain(channel);
    AscendC::PipeBarrier<PIPE_ALL>();
    uint64_t b0 = AscendC::GetSystemCycle();
    for (i = 0; i < ITERS; ++i) hcomm.WriteNbi(channel, dstSlot, srcBuf, MSG_LEN);
    hcomm.Drain(channel);                              // 等全部完成再停表
    AscendC::PipeBarrier<PIPE_ALL>();
    uint64_t b1 = AscendC::GetSystemCycle();
    // e2e_avg[MSG_LEN] = (b1 - b0) / ITERS；带宽 = MSG_LEN / e2e_avg
}
```
- **ReadNbi**：`hcomm.ReadNbi(channel, readBuf, dstSlot, MSG_LEN)`（需一片 `readBuf` 落读回数据；方式 A 同样不 Drain）。
- **WriteWithNotifyNbi**：`hcomm.WriteWithNotifyNbi(channel, dstSlot, srcBuf, MSG_LEN, notifyDst, val)`（占 2 WQEBB，方式 A 的 ITERS 注意别超 SQ）。

#### K6.3 精度校验
不做逐字节数据校验，只校验：所有接口返回码为 0、无 PollCq 超时；方式 A 得到提交时延、方式 B 得到端到端时延/带宽，数值落在合理区间。三接口 × 两方式的结果分别记录到 context。

### K7 — Batch 接口性能测试（`kernel_urma_perf_batch`）
目的：批量提交的吞吐优势。每个 batch 内攒 `BATCH_N` 个 WQE 一次 BatchCommit，warmup 后计时。建议 `WARMUP = 10` 个 batch、`ITERS = 200` 个 batch、`BATCH_N = min(sqDepth-1, 32)`。

#### K7.1 数据存放方式
```cpp
GM_ADDR srcBuf = scratch;   // 发送源：BATCH_N*MSG_LEN 字节，任意 pattern（不校验内容）
```
**初值要求**：无需特定初值；对端落点不比对。

#### K7.2 测试接口伪码
```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);
auto batchHandle = hcomm.MakeBatchHandle(channel, batchTensor, BATCH_LEN, remoteBase, localBase);

// warmup
for (b = 0; b < WARMUP; ++b) {
    for (j = 0; j < BATCH_N; ++j) hcomm.WriteNbi(batchHandle, dstSlot + j*MSG_LEN, srcBuf, MSG_LEN);
    hcomm.BatchCommit(batchHandle);
    hcomm.Drain(batchHandle);
}

// 计时
AscendC::PipeBarrier<PIPE_ALL>();
uint64_t t0 = AscendC::GetSystemCycle();
for (b = 0; b < ITERS; ++b) {
    for (j = 0; j < BATCH_N; ++j) hcomm.WriteNbi(batchHandle, dstSlot + j*MSG_LEN, srcBuf, MSG_LEN);
    hcomm.BatchCommit(batchHandle);
    hcomm.Drain(batchHandle);
}
AscendC::PipeBarrier<PIPE_ALL>();
uint64_t t1 = AscendC::GetSystemCycle();
// avg_per_wqe = (t1 - t0) / (ITERS * BATCH_N)
```

#### K7.3 精度校验
不做逐字节数据校验，只校验：返回码为 0 + 计时结果合理；可与 K6 单发 avg 对比，验证 batch 每 WQE 均摊时延更低。

### K8 — Lock/Unlock 多核互斥（`kernel_urma_lock`）
目的：验证同设备**多个 AI Core 共享同一通道**时，`Lock`/`Unlock` 能互斥保护对通道状态的更新。单核跑测不出互斥语义，故必须多核并发同一 channel。V310 支持（本仓库）。

#### K8.1 数据存放方式
```cpp
GM_ADDR srcBuf = scratch;   // 发送源：256B，64×int32，全填 10 + myRank
// 所有 AI Core 共享同一 channel，各核用同一 dstSlot 下按 blockIdx 分格，避免核间落点互踩
uint32_t coreId = AscendC::GetBlockIdx();      // 本 AI Core 号
constexpr uint32_t CORE_STRIDE = 256;          // 每核在 dstSlot 内独占一格
GM_ADDR myCoreDst = dstSlot + coreId * CORE_STRIDE;
```
**初值要求**：`srcBuf` 填 `10 + myRank`；本卡接收区 `recvSlot + coreId*256`（各核落点）清 `0`。互斥保护的对象是**通道内部状态**（sqHead/sqTail 等），不是数据落点本身。

#### K8.2 测试接口伪码
```cpp
hcomm.Init(workspaceTensor, WORKSPACE_SIZE);   // 每核各自 Init 自己的 UB 工作区

// 多个 AI Core 并发执行以下序列（同一 channel）：
hcomm.Lock(channel);                            // 阻塞直到拿到锁；不保证获取顺序
// —— 临界区：更新通道状态的操作必须在锁内 ——
hcomm.WriteNbi(channel, myCoreDst, srcBuf + 0, 256);
hcomm.Unlock(channel);                          // 必须由同一 AI Core 释放；异常路径也要释放

hcomm.Drain(channel);
```

约束：锁**不可重入**（释放前不得再 Lock 同一 channel）；成功 Lock 后必须由**同一 AI Core** Unlock。

#### K8.3 精度校验
一律读本地：本卡 `recvSlot` 里是对端各核写进来的数据。

- 每核 `Lock`/`WriteNbi`/`Unlock` 返回码均为 `0`，无死锁 / 无超时。
- 各核数据到位：本卡在 `recvSlot + coreId*256`（对端每个核一格）校验 == `10 + peerRank`（每核一格互不覆盖 ⇒ 互斥期间通道状态未被并发破坏）。
- 反向观察：若去掉 Lock/Unlock 并发跑，通道状态竞争可能导致丢包 / 返回码异常（可作为对照，非必须）。

## 5. 通用校验原则（跨 kernel）

各 kernel 自己的数据排布 / 初值 / 校验清单已下沉到该 kernel 的 `.1 数据存放方式` 与 `.3 精度校验`。此处只列所有 kernel 共同遵守的原则：

- **校验一律读本地**（真机核心约束）：本卡只能读自己 `localBase` 内的数据（接收区 `recvSlot` + 本卡私有 scratch 三片）；写出到对端的 `dstSlot` 本卡读不到。**两卡跑相同代码**，所以每张卡都只校验本地：
  - `recvSlot` 里的数据是**对端写进来的**，预期 = 对端源值。2P 下对端 = peerRank，其源值按各 kernel 约定（K1/K3/K5/K8 源 = `10 + rank`）→ 本卡预期 = `10 + peerRank`。
  - `readBuf` 是本卡 ReadNbi 读回的，预期 = 本卡自己写出去、又读回来的值。
  - `fetchBuf` 是本卡 atomic 的旧值回写，预期 = 对端该地址在本卡 atomic 生效前的值。
- **接收区初值由本卡自己清**：清的是本卡自己的 `recvSlot`（对端来写、本卡校验），不是写目标 `dstSlot`。两卡各清各的即全部就绪。
- **返回码**：任一接口 `!= HCOMM_SUCCESS` 立即写 `testResult` 错误码并短路返回。
- **性能 kernel（K6/K7）** 不做逐字节数据校验，只校返回码 0 + 计时合理。
- **收尾同步**：两卡各自 Drain 后，需要一次 host 侧 barrier（或 kernel 内 barrier flag）确保对端已把数据写完，本卡再读 `recvSlot` 校验，避免“我读时对端还没写到”。

## 6. 多次 kernel launch 循环（维度 4）
不新增 kernel，由 host 侧对上述任一功能 kernel（建议用 **K1**，覆盖面最广）重复 launch，验证控制面多次建链/复用不泄漏、不错乱。
- 建议循环次数：**100 次**（快速冒烟）/ **1000 次**（夜间回归）。
- 每次 launch 前后检查 channel 状态（sqHead/sqTail/cqTail）是否归位，返回码是否稳定为 0。
- 可与 K4 结合：每次 launch 轮换一个 sqDepth 配置。

## 7. 落地清单
- [x] `odr` 语义：`5=relax / 6=strong`（以此为准，仓库注释有误）。已固化到 §2.4。
- [x] `WriteReduceNbi` 支持 dtype/op：`{int8,int16,int32,uint32,half,float,bf16} × {MAX,MIN,SUM}`，`inlineEn=0`。K1 用一片 int32 src 覆盖 int32 SUM + int32 MAX。
- [x] V310 支持 `Lock/Unlock`（本仓库）。已独立为 K8（多核并发同一 channel）。
- [x] 性能打点：`AscendC::PipeBarrier<PIPE_ALL>()` + `AscendC::GetSystemCycle()`。已用于 K6/K7。
- [x] sqDepth 由 host 注入，取值 [16,8192] 的 2 的幂，选 `{16,64,512,8192}`。
- [ ] **Batch 接口平台支持待确认**：API doc 标注“仅 Ascend 950”。若 V310 亦支持（同 odr/Lock 的仓库勘误情况），K2/K7 直接可用；否则 K2/K7 需在 950 上跑或标记 skip。
- [x] `WriteReduceNbi` 远端 reduce 校验基准：初值 + 期望已在 K1.1/K1.3 固化（SUM 初值 0、MAX 初值 INT32_MIN，源 = 10+rank）。
- [ ] **float/half/bf16 reduce 覆盖（可选扩展）**：K1 现用一片 int32 src，只覆盖 int32 SUM/MAX。如需覆盖浮点 reduce，另留一小片 float src 单独测（源不能与 int32 片共用，bit 解释不同）。
- [ ] 若需覆盖“多卡对同一地址原子归约（SUM 叠加）”语义，需另设一个**不分槽的共享 dst** 用例（当前按 sender rank 分槽是单卡语义，不叠加）。
- [ ] **K6 数据量上限 256M 需注册内存支撑**：单次 256M 要求本卡 `srcBuf` 与对端 `dstSlot` 落点各能容纳 256M。若实际注册内存 < 256M，K6 方式 B 的数据量上限相应下调到 ≤ 注册内存大小（host 侧确认后调整扫描表）。

---

## 附录 A：本次需求原文（留档）

> 我现在想要设计一套冒烟用例，维护 aiv 直驱 urma 接口的功能。一些测试项放在一个 kernel 里面，需要你帮我列举出来每个 kernel 里面的测试接口的调用（伪码，不用管 kernel 内其他东西，比如说我要测试普通 write 接口，你就给我列出来类似于：hcomm.Init(localTensor); hcomm.WriteNbi(channelHandle, remoteAddr+4, localAddr, 4); hcomm.Drain(channelHandle); 这样的就够了）。我现在初步想的要测试这些维度：
> 1. 所有提供的接口的普通功能测试，
> 2. 这些接口分别进行压测，及循环调用超过 sqDepth 深度的次数，
> 3. 控制面如果设置不同 sqDepth x 是否延迟 commit（commit=false 和 commit=true/commit 接口混用）x 产生 cqe 或者不产生 cqe 组合测试，
> 4. 启动多次 kernel launch 循环测试，这个主要是控制面的多启动几次，你可以只给出循环多少次的建议即可，
> 5. 保序功能测试，比如说前面默认调用 write 接口，最后一次调用 write 接口但是传入 ord 是 6，只判断 6 这次的数据到了，6 这次数据到位了前面的数据就也应该默认就位了，注意 5 是 relax order，6 是 strong order，
> 6. 普通接口性能测试，你就给出 warmup 几次，实际循环几次取平均之类的，
> 7. batch 接口性能测试。
>
> 以上是我给出的几个维度，你可以进行适当扩展，但是千万不要扩展过头了，只保留必要的，然后告诉我设计几个 kernel，每个 kernel 里面放哪些测试内容，一些简单的可以合并成一个 kernel 测试。结果生成到文档里面，把我这次的提问内容也保存进文档留档。

