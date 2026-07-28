# CCU_IF 结构导致 AllGather Mesh1D Mem2Mem 单向丢数据 —— 问题分析

## 一句话结论

在 CCU AllGather Mesh1D Mem2Mem kernel 里，把 **remote write 的 rank 循环放进
逐 peer 的 `CCU_IF` 分支**（而不是用一个 `CCU_IF` 包住整个循环），会让 CCU 生成
错误的条件图 / 事件依赖，导致**一个方向的 remote write 整段丢数据**。改回
"单个 `CCU_IF(sliceSize != 0)` 包住整段传输" 后完全正确。

想请教的核心问题：**`CCU_IF` 放在 rank 循环内部、逐个 peer 各生成一个条件块，
和把整个循环包在一个 `CCU_IF` 里，两者生成的 CCU 图语义为什么不等价？**

---

## 复现环境

- 平台：Ascend 950DT（dav-3510），双卡，`HCCL_OP_EXPANSION_MODE=CCU_SCHED`
- 算法：AllGather Mesh1D Mem2Mem，host 直驱（`HcommCcuKernelRegister` +
  `HcommCcuKernelLaunch`），CCU kernel 用 asc-comm 的 `ccu::` 表示层原语
- 数据面原语：`ccu::Write`（remote write）、`GroupCopy`（本地 LoopGroup 搬运）、
  `ccu::EventRecord` / `ccu::EventWait`、`ccu::NotifyRecord/NotifyWait`（前后同步）

## 现象

输入构造 `input[i] = rankId * 1000000 + i + 1`（**逐元素不同**，是关键）。
count=2、双卡，期望每个 rank 的 output = `[1, 2, 1000001, 1000002]`。

**出错版本：**
```
rank 0 output: [ 1 2 1000001 1000002 ]   ← 正确
rank 1 output: [ 0 0 1000001 1000002 ]   ← 前两个是 0（rank0 远程写来的那段整段丢失）
```
- rank1 的后半 `[1000001,1000002]` 是它自己的数据，靠本地 GroupCopy 写入 → 正确
- rank1 的前半应由 **rank0 远程写入**，结果是 memset 的 `[0,0]` → **rank0→rank1 的
  remote write 整段没生效**
- 反方向 rank1→rank0 正常（rank0.output 后半正确）
- 伴随一条**仅 device 0** 的报错：
  `[Hcomm][RemoveSingleChannel] failed to find handle mapping in g_ChannelD2HMap, deviceId[0]`
- 退出码 -11（SIGSEGV，收尾阶段崩溃）

**为什么之前一直没发现：** 参照样例 `custom_ccu_allgather` 的输入是
`input[i] = device`（**同一 rank 内所有元素相同**），即使 remote write 只写对
部分元素，结果也看不出差异。把它的输入改成逐元素不同后，它**也能正确**——
说明 kernel 数据面本身没问题，问题在我们改写的传输结构。

## 出错的写法（remote write 循环在逐 peer 的 CCU_IF 内）

```cpp
static void DoAllGather(Ctx &ctx, const LocalAddr &src, const LocalAddr &localDst,
                        const std::vector<RemoteAddr> &dst, const Variable &sliceSize)
{
    uint32_t channelId = 0;
    for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        const uint16_t mask = 1 << rankIdx;
        if (rankIdx == ctx.arg->rankId) {
            CCU_IF(ctx.isInputOutputEqual != 0) {          // 条件块 A
                ccu::EventRecord(ctx.event, mask);
            }
            CCU_IF(ctx.isInputOutputEqual == 0) {          // 条件块 B
                GroupCopy(ctx, localDst, src, ctx.goSize);
                ccu::EventRecord(ctx.event, mask);
            }
        } else {
            CCU_IF(ctx.normalSliceSize != 0) {             // 条件块 C（每个 peer 一个）
                ccu::Write(ctx.arg->channels[channelId], dst[rankIdx], src, sliceSize,
                           ctx.event, mask);
            }
            channelId++;
        }
    }
    const uint16_t totalMask = (1 << ctx.arg->rankSize) - 1;
    ccu::EventWait(ctx.event, totalMask);
}
```

特征：
- `CCU_IF` 在 `for (rankIdx)` **循环体内部**，每个 peer 各生成一个 `CCU_IF` 条件块
- self 分支里用两个互斥 `CCU_IF(ioEqual!=0)` / `CCU_IF(ioEqual==0)` 生成两条路径，
  各自对**同一个 event** 做 `EventRecord`
- 多个条件块穿插 `EventRecord` / `Write` / `GroupCopy`，最后统一 `EventWait`

## 正确的写法（单个 CCU_IF 包住整个传输，与 custom 逐字节一致）

```cpp
static CcuResult ExecuteAllGatherTransfer(Ctx &ctx)
{
    LocalAddr src, localDst;
    std::vector<RemoteAddr> dst;
    dst.resize(ctx.arg->rankSize);

    src.addr = ctx.input; src.addr += ctx.currentRankSliceInputOffset;
    src.token = ctx.token[ctx.arg->rankId];
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr = ctx.output[rankIdx]; localDst.addr += ctx.currentRankSliceOutputOffset;
            localDst.token = ctx.token[rankIdx];
        } else {
            dst[rankIdx].addr = ctx.output[rankIdx]; dst[rankIdx].addr += ctx.currentRankSliceOutputOffset;
            dst[rankIdx].token = ctx.token[rankIdx];
        }
    }

    CCU_IF(ctx.normalSliceSize != 0)                       // 单个条件块，包住整个循环
    {
        uint32_t channelId = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask = 1 << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                ccu::Write(ctx.arg->channels[channelId], dst[rankIdx], src,
                           ctx.normalSliceSize, ctx.event, mask);
                channelId++;
            }
        }
    }

    GroupCopy(ctx, localDst, src, ctx.goSize);             // 无条件本地拷贝
    ccu::EventRecord(ctx.event, 1 << ctx.arg->rankId);     // self 只 Record 一次

    const uint16_t totalMask = (1 << ctx.arg->rankSize) - 1;
    ccu::EventWait(ctx.event, totalMask);
    return CCU_SUCCESS;
}
```

特征：
- **一个** `CCU_IF(sliceSize != 0)`，内部是完整的 remote write 循环
- 本地 GroupCopy **无条件**执行（不放进任何 CCU_IF）
- self 只做一次 `EventRecord`，无 isInputOutputEqual 分支

**改动结果：** 两个 rank output 全对，退出码 0，`RemoteSingleChannel` 报错和
SIGSEGV 一并消失。除此之外 host 侧、数据布局、channel、taskArgs 全未改动。

## 两种写法的差异（只有这一处）

| 维度 | 出错版 | 正确版 |
|---|---|---|
| CCU_IF 位置 | rank 循环**内**，每 peer 一个 | rank 循环**外**，一个包全部 |
| CCU_IF 数量 | 每 peer 1 个 + self 2 个（ioEqual 互斥） | 1 个 |
| 本地 GroupCopy | 放在 `CCU_IF(ioEqual==0)` 内 | 无条件 |
| self EventRecord | 在条件块内（两条互斥路径各一次） | 条件块外，一次 |
| remote write 结果 | 一个方向整段丢失 | 全部正确 |

## 想请教的问题

1. **`CCU_IF` 的语义**：它是不是把包住的操作编译成一个"条件执行的 CCU 图节点/子图"？
   如果是，为什么在 `for` 循环里对每个 peer 分别 `CCU_IF(...)`（生成 N 个条件块）
   与用一个 `CCU_IF` 包住整个循环（1 个条件块）语义不同？循环内多次 `CCU_IF` 是否
   会产生互相干扰、覆盖、或调度顺序问题？

2. **event 依赖**：同一个 `ccu::Event` 在**多个不同 CCU_IF 条件块**里分别
   `EventRecord`（不同 mask 位），最后一次 `EventWait(totalMask)` —— 这种"跨多个
   条件块记录同一 event"是否会破坏 CCU 的事件依赖图？正确用法是否要求一个 event 的
   所有 Record 在同一控制流层级？

3. **remote write 与条件块**：`ccu::Write`（remote）放进循环内逐个 `CCU_IF`，是否
   会导致部分 Write 未被正确挂到图上/未提交？为什么恰好表现为**单方向**丢失而不是
   两个方向都丢？

4. **CCU_IF 使用规范**：是否有官方约束——例如 "CCU_IF 不能出现在 host 侧 for 循环
   体内"、"CCU_IF 内的操作数量/类型有限制"、"分支条件必须是编译期常量或特定
   Variable"？我们的 `isInputOutputEqual` / `normalSliceSize` 都是 `LoadArg` 进来的
   运行期 `Variable`，用它做 `CCU_IF` 条件是否合法？

5. **`RemoveSingleChannel ... deviceId[0]`**：这条报错是否是上述错误图的必然结果
   （channel 状态未正确收尾）？还是另一个独立问题？

## 附：与参照样例的关系

- 正确版的 `ExecuteAllGatherTransfer` 与仓库内**已验证可跑通**的
  `examples/custom_ccu_allgather/op_kernel_ccu/ccu_kernel.cc` 中的同名函数**逐字节
  等价**。
- 出错版是移植 asc-devkit/pr_4261 `ccu_kernel_all_gather_mesh1d_mem2mem.cc` 时，为
  支持 repeat 分片 / isInputOutputEqual / 尾块而引入的结构（把传输拆进 `DoAllGather`
  并在循环内用 CCU_IF）。pr_4261 原版把这些逻辑写在 `CcuKernelAlgBase` 类式框架上，
  我们改写到 `ccu::` 自由函数表示层时改变了 CCU_IF 的嵌套层级——这可能是问题引入点。
- 因此后续若要加回 repeat / isInputOutputEqual / 尾块能力，需要在**不把 CCU_IF 放进
  rank 循环**的前提下重新组织控制流。

---

# 问题二：CCU_WHILE 多轮 repeat，第 2 轮数据整段丢失

## 一句话结论

单轮传输（repeatNum=1）完全正确并已真机验证。但把传输体放进设备端
`CCU_WHILE` 循环做**多轮**搬运（repeatNum=2）时，**第 2 轮的数据整段丢失**，
第 1 轮正确。

想请教的核心问题：**`CCU_WHILE` 设备端循环体内做 `ccu::Write`（remote write）+
每轮复用同一个 `ccu::Event`（EventRecord/EventWait），正确写法是什么？为什么第 2
轮没生效？**

## 现象

count=2、双卡，把每个 rank 的数据切成 2 轮（repeatNum=2，每轮 1 个元素，
`inputRepeatStride = outputRepeatStride = 4` 字节）。期望每 rank output =
`[1, 2, 1000001, 1000002]`：

```
rank 0 output: [ 1 0 0 0 ]         期望 [1 2 1000001 1000002]
rank 1 output: [ 1 0 1000001 0 ]   期望 [1 2 1000001 1000002]
```

规律：**每段的第 0 个元素（第 1 轮搬的）正确，第 1 个元素（第 2 轮搬的）全为 0
（memset 初值，即第 2 轮完全没写入）**。
- rank0.output[0]=1（轮1 本地拷贝）✓，output[1]（轮2）✗
- rank1.output[0]=1（轮1 rank0 远程写来）✓，output[1]（轮2）✗；
  output[2]=1000001（轮1 rank1 本地）✓，output[3]（轮2）✗

## 出错的写法（CCU_WHILE 包住整个传输体，每轮 Write + 复用 event）

```cpp
static CcuResult ExecuteAllGatherTransfer(Ctx &ctx)
{
    // ... 循环外：建好 src / localDst / dst[] 的初始地址和 token ...

    CCU_WHILE(ctx.tmpRepeatNum != UINT64_MAX)   // 设备端循环，tmpRepeatNum = UINT64_MAX - repeatNum
    {
        ctx.tmpRepeatNum += ctx.constVar1;       // Variable += 1

        CCU_IF(ctx.repeatTimeFlag != 0)          // 第 2 轮起才推进地址
        {
            src.addr += ctx.inputRepeatStride;
            localDst.addr += ctx.outputRepeatStride;
            for (peer) dst[peer].addr += ctx.outputRepeatStride;
        }

        CCU_IF(ctx.normalSliceSize != 0)         // 单个 CCU_IF 包 remote write 循环（问题一的正确结构）
        {
            for (peer) ccu::Write(channel[peer], dst[peer], src, ctx.normalSliceSize, ctx.event, mask);
        }

        GroupCopy(ctx, localDst, src, ctx.goSize);          // 本地拷贝
        ccu::EventRecord(ctx.event, 1 << ctx.arg->rankId);  // 每轮对同一个 event Record
        ccu::EventWait(ctx.event, (1 << rankSize) - 1);     // 每轮 Wait 同一个 event

        ctx.repeatTimeFlag = 1;
    }
    return CCU_SUCCESS;
}
```

- `tmpRepeatNum` / `repeatTimeFlag` / `constVar1` / `src.addr` 等都是 CCU 表示层
  `Variable`；`repeatTimeFlag` 初值 0，循环末尾置 1，意图让第 2 轮起推进地址。
- 移植自 asc-devkit/pr_4261 的 `DoRepeatAllGather`（原版写在 `CcuKernelAlgBase`
  类框架上，我们改写到 `ccu::` 自由函数表示层）。

## 已排除 / 已确认

- **单轮已验证**：把 repeatNum 固定为 1（`CCU_WHILE` 只跑 1 轮，`repeatTimeFlag`
  分支不触发），count=2 / 8192 / 65539(尾块) / 单卡 全部逐元素校验通过。所以
  `CCU_WHILE` 只跑一轮时不破坏正确性，问题只在**第 2 轮**。
- `CCU_WHILE` 宏展开（include/ccu/ccu_control_flow_macro.h）显示：循环条件里的
  `Variable` handle 被交给运行时 `CcuWhileBegin`，循环体录制成 CCU 图由设备按条件
  反复执行——即这是**设备端循环**，循环体内必须都是能被录进图的 CCU 表示层操作。

## 想请教的问题

1. **`CCU_WHILE` 循环体内做 `ccu::Write`（remote write）合法吗？** 每轮写不同远端
   地址（靠 `Variable += stride` 推进），第 2 轮为何整段没生效？是地址推进没被录进
   循环图，还是 Write 本身在循环体内有限制？
2. **event 复用**：同一个 `ccu::Event` 在 `CCU_WHILE` 每轮都 `EventRecord` +
   `EventWait`，是否合法？还是每轮必须用不同 event / 不同 mask，或有专门的循环内
   同步原语？
3. **`CCU_WHILE` 内嵌 `CCU_IF`**：循环体里再用 `CCU_IF(repeatTimeFlag != 0)` 做
   "第 2 轮起推进地址"，这种嵌套在图构建上支持吗？会不会导致地址推进只在录制期
   执行一次、而非每轮生效？
4. **`Variable` 的跨轮语义**：`repeatTimeFlag = 1`、`src.addr += stride` 这类赋值/
   自增，在设备端循环里是每轮累积生效，还是需要特定写法（如专用累加原语）才能跨轮
   保持？
5. **有没有 `CCU_WHILE` 里做集合通信多轮搬运的正确范例？** pr_4261 有这套 repeat
   代码，但我们在 asc-comm 直驱 + `ccu::` 自由函数下没跑通——想确认是用法错，还是
   这条路需要不同的实现方式（例如 host 端多次 launch 而非 kernel 内循环）。

## 已排查并排除的假设（读源码确认，供对方跳过）

- **假设「远端地址走立即数、不随循环推进」→ 否。** `RemoteAddr.addr` 与
  `LocalAddr.addr` 同为 `Address` 类型（include/ccu/ccu_remote_addr.hpp、
  ccu_local_addr.hpp），`Address::operator+=(Variable)`（ccu_address.hpp）统一走
  `CcuAddressAddAssignVar` → `CcuRepAdd(SELF_ADD_ADDRESS)` → `LoadGSAXnInstr`。
  远端与本地地址推进机制相同，不是「远端用立即数」。
- **假设「std::vector<RemoteAddr> dst.resize() 浅拷贝致 handle 共享」→ 否。**
  `dst` 初始为空，`resize(rankSize)` 对空 vector 是 value-initialization（逐个
  `RemoteAddr()` → 各自 `CcuRemoteAddrAlloc` 独立 handle），非拷贝填充；且
  custom_ccu_allgather 用同样的 `dst.resize()` 单发可跑通。

## pr_4261 多轮 repeat 的实现位置与证据（路径均在 asc-devkit @ pr_4261）

前提：pr_4261 的多轮 repeat 视为已调通（用户确认）。它用的是一套**与 asc-comm
`ccu::` 不同的 CcuRep 接口**，且通过 dlsym 转发到**装机 CCU 运行时库**，不是编译
asc-comm 源码。逐部件证据：

1. **CCU_WHILE = `CcuRep::Repeat` 对象**（非 asc-comm 的纯宏）
   `include/adv_api/hccl/internal/hcomm/pkg_inc/hcomm/ccu/ccu_repeat_v1.h:20`
   ```cpp
   #define CCU_WHILE(x) \
       for (auto __ccuRepeatHidden = CcuRep::Repeat(this, x); \
            __ccuRepeatHidden.Check(); __ccuRepeatHidden.Run())
   ```
   `class Repeat`（同文件 :24）持有 beginLabel/endLabel/jump 三个 rep 成员，
   构造传 `this`（CcuRepContext*），由对象生命周期管理循环录制。
   对比 asc-comm：`include/ccu/ccu_control_flow_macro.h` 的 CCU_WHILE 是纯宏展开
   （`__COUNTER__` 生成 label + `_CcuDoWhileStackPopForWhile` 全局栈），无 Context 对象。

2. **非阻塞写 `WriteNb` + 有状态 `CompletedEvent`**（非 asc-comm 的 ccu::Write/Event）
   `include/adv_api/hccl/internal/hcomm/pkg_inc/hcomm/ccu/ccu_kernel.h:139,142`
   （WriteNb 两个重载，参数为 `CcuRep::CompletedEvent event`）；
   `:125,126` `RecordEvent(CompletedEvent)` / `WaitEvent(CompletedEvent)`。
   `CompletedEvent` 带 `SetMask`（有状态 event 对象）。
   对比 asc-comm：只有 `ccu::Write`（`ccu_primitives.hpp:104`，走 CcuWriteMemToMem）
   和 `ccu::Event`（mask 传参），**无 WriteNb / 无 CompletedEvent**。

3. **多轮调用点**（这套接口如何组织 repeat）
   `impl/adv_api/detail/hccl/cc/src/ops/all_gather/template/ccu/kernel/ccu_kernel_all_gather_mesh1d_mem2mem.cc`
   - `:162 DoRepeatAllGather()` → `:183 CCU_WHILE(tmpRepeatNum_ != UINT64_MAX)`
     内 `:197 CCU_IF(normalSliceSize_ != 0){ DoAllGather(...) }`
   - `:131 DoAllGather()` 里 **CCU_IF 就放在 per-peer rank 循环内**
     （`:138/:143 CCU_IF(isInputOutputEqual_...)`、`:150 CCU_IF(normalSliceSize_)`），
     `:153 WriteNb(channels_[channelId], dst[rankIdx], src, sliceSize, event_)`，
     `:141/:147 RecordEvent(event_)`，`:159 WaitEvent(event_)`。
   **注意：pr_4261 的 CCU_IF 也在 rank 循环内**——与本文「问题一」里 asc-comm 下
   出错的结构相同，但 pr_4261（CcuRep + 装机运行时）能跑通。这说明「CCU_IF 放
   循环内」在 CcuRep 那套里合法，在 asc-comm `ccu::` 那套里出错——**两套表示层
   /运行时行为不一致**。

4. **dlsym 转发层（证明调的是装机库，不是编 asc-comm 源码）**
   `impl/adv_api/detail/hccl/cc/src/common/hcomm_dlsym/ccu/` 下整套 `*_dl.hpp`
   （ccu_address_dl.hpp / ccu_control_flow_macro_dl.h / ccu_event_dl.hpp /
   ccu_remote_addr_dl.hpp 等）——CcuRep 接口经此 dlsym 到装机 CCU 运行时符号。

**据此的判断**：pr_4261 的「WriteNb（非阻塞）+ CompletedEvent（有状态）+
Repeat 对象版 CCU_WHILE + dlsym 到装机运行时」是一套整体；asc-comm 的
「ccu::Write + ccu::Event + 宏版 CCU_WHILE + 编译源码运行时」是另一套。多轮 repeat
在前者通、在后者第 2 轮丢数据，差异根源很可能在**这两套表示层/运行时的实现不
一致**（尤其 CCU_WHILE 循环体内 CCU_IF + 写 + event 的图录制），**而非样例用法**。
若属实，修复点在 asc-comm `ccu::` 库实现，不在本样例。

## 收敛后最想确认的一点

`Address::operator+=(Variable)`（`CcuAddressAddAssignVar` → `SELF_ADD_ADDRESS` →
`LoadGSAXnInstr`）放在 `CCU_WHILE` 设备端循环体内时，**是否每轮都对同一个 Address
（GSA 寄存器）真实累加**？还是循环体只录制一次、`SELF_ADD_ADDRESS` 只在录制期算
一次、设备端多轮不重复执行？这直接决定 kernel 内多轮 repeat 能否成立；若不成立，
是否应改为 **host 端按硬件单传输上限多次 `HcommCcuKernelLaunch`**（每轮一次 launch、
kernel 只做单发）——这正是 custom_ccu_allgather `ExecOp` 里 host `for` 循环切
`UB_MAX_DATA_SIZE` 的做法。

## 现状

- **单轮版（repeatNum=1）= 交付基线**，已真机验证。
- **多轮 repeat = 已实现但真机不通**，host 已锁回 repeatNum=1；kernel 保留
  `CCU_WHILE` 结构（单轮走通），等 CCU_WHILE 多轮语义澄清后再启用。
- 参照：仓库内 `examples/custom_ccu_allgather` 只做单轮（无 repeat），是本机验证
  可跑的最小实现；多轮 repeat 在本机无已知可跑范例。
