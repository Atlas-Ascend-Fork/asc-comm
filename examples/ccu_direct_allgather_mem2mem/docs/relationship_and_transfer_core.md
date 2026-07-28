# 三者关系（pr_4261 / custom_ccu_allgather / 本样例）与「传输核心」单发 vs 完整版

本文说明两个问题：

1. 本样例、`examples/custom_ccu_allgather`、`asc-devkit/pr_4261` 三者在实现上的
   血缘关系。
2. kernel「传输核心」里，**完整版（pr_4261）** 和 **单发版（本样例 / custom）**
   到底差在哪。

---

## 一、三者血缘关系

```
asc-devkit/pr_4261  (源头：完整功能，但装机 CANN 跑不起来)
        │
        │  两个样例都从它移植 AllGather Mesh1D Mem2Mem 数据面
        ├──────────────────────────────┐
        ▼                              ▼
custom_ccu_allgather              本样例 ccu_direct_allgather_mem2mem
(仓库自带，已验证可跑)             (在 custom 与 pr_4261 之间收敛)
```

- **pr_4261** 是共同源头：`GroupCopy`、`ccu::Write` 远程写、前后同步、`CalGoSize`
  切分算法都出自这里。它写在 `CcuKernelAlgBase` 类式框架上，功能完整
  （repeat / isInputOutputEqual / 尾块），但依赖 `InitV2` 那套，装机 CANN 跑不通。
- **custom_ccu_allgather** = pr_4261 的**最小可跑子集**：砍掉 repeat / 原地优化 /
  尾块，类框架改写成 `ccu::` 自由函数，taskArgs 精简到 10 个，host 用
  `AllocAlgResource → InitAlgResourceCtx(engine context) → GetCcuKernel(注册) →
  ExecOp(launch)`。是别人已趟通、可在本机运行的参考实现。
- **本样例** 从 pr_4261 移植，最初想保留完整功能（15 个 taskArgs + repeat +
  isInputOutputEqual），但那套结构在直驱模式下踩了 `CCU_IF` 的坑（单向丢数据，
  见 [CCU_IF_bug_analysis.md](./CCU_IF_bug_analysis.md)）。最终**传输核心收敛到
  与 custom 逐字节一致**，只在 taskArgs 接口上保留 pr_4261 的完整字段作预留。

| | pr_4261 | custom_ccu_allgather | 本样例 |
|---|---|---|---|
| 定位 | 完整功能源头 | pr_4261 最小可跑子集 | pr_4261 移植 + 向 custom 收敛 |
| kernel 写法 | 类框架 CcuKernelAlgBase | ccu:: 自由函数 | ccu:: 自由函数 |
| 传输核心 | 完整（repeat/原地/尾块） | 单发 | 单发（与 custom 逐字节一致） |
| taskArgs | 完整 | 10 个 | 15 个（含 pr_4261 预留字段，加载不用） |
| repeat / isInputOutputEqual | 有 | 无 | 无（字段预留） |
| 能跑通 | ✗（装机 CANN） | ✓ | ✓ |

---

## 二、「传输核心」：单发版 vs 完整版

「传输核心」= kernel 里真正搬数据的那段逻辑：**怎么把每个 rank 的输入搬到所有
rank 的输出**。不含建域、注册、同步等外围。

### 单发版（本样例 / custom）

一次调用把**整块数据**搬一遍：

```text
ExecuteAllGatherTransfer():
    src = 本 rank 的输入
    CCU_IF(sliceSize != 0):
        对每个 peer:  ccu::Write(src 整块 -> peer 的 output)    // 一次写全部
    本地 GroupCopy(src 整块 -> 自己 output 的对应段)            // 一次拷全部
    EventRecord(self)
    EventWait(所有 rank)
```

- `sliceSize` = 整个 count 的字节数，`ccu::Write` 一次写完。
- 搬运恒 1 轮。

### 完整版（pr_4261）= 单发版外面套两层循环 + 两个分支

```text
DoRepeatAllGather():
    CCU_WHILE(还有 repeat 未完成):                 // ← 外层多轮
        推进 src/dst 地址（加 repeatStride）
        DoAllGather():                             // ← 内层才是"单发版"
            对每个 peer:
                if (in == out) 只 EventRecord      // ← isInputOutputEqual 分支
                else           ccu::Write + 本地 GroupCopy
            EventWait
```

多出来的三样：

**1. repeat 分片（最本质区别）。** 把一次大传输拆成多轮。原因：硬件单次
remote write 有上限（custom 里 `UB_MAX_DATA_SIZE = 256MB`）。一个 rank 的数据超过
上限时，单发一次 `ccu::Write` 写不下，需分 N 轮，每轮靠 `repeatStride` 挪地址。
`repeatNum` = 轮数。**单发版 = repeatNum 恒为 1。**

**2. isInputOutputEqual（原地优化）。** 当输入输出是同一块内存（in-place）时，
「把自己的数据拷到自己的输出段」是多余的，完整版跳过本地 GroupCopy 只记 event。
单发版无条件拷一次：in≠out 时本就该拷；in==out 时多拷一次也不出错，只是没优化。

**3. 尾块 lastSliceSize。** 数据量非整块对齐时，最后一小块单独处理。**但实测发现
单发版不需要专门写它**：host 的 `CalGoSize` 已把 size 切成 m 个整块 + n 个 memSlice
+ p 尾巴，`GroupCopy` 按这套参数天然覆盖尾块，因此 count=65539（带尾巴）也正确。

### 对照

| | 单发版（本样例/custom） | 完整版（pr_4261） |
|---|---|---|
| 搬运轮数 | 恒 1 轮，整块一次搬完 | 可多轮（超硬件上限时分批） |
| repeat 循环 | 无 | 有（CCU_WHILE + repeatStride） |
| in==out 优化 | 无（无条件本地拷贝） | 有（跳过冗余拷贝） |
| 尾块 | 靠 CalGoSize 覆盖，无专门逻辑 | 有 lastSliceSize 专门处理 |
| 适用数据量 | ≤ 单次传输上限（本样例场景足够） | 任意大（超大数据分批） |
| CCU 图复杂度 | 低（单层） | 高（循环 + 分支嵌套） |

### 一句话

**单发版 = 完整版在「repeatNum=1、in≠out」时的特例。** 本样例数据量小
（最大 256KB/rank ≪ 256MB 上限）且 in≠out，特例即够用；砍掉外面两层循环不影响
正确性，反而避开了 `CCU_IF` 嵌套 rank 循环的坑。

若日后要加回完整版能力（超大数据 / 原地 / 显式尾块），务必：
- repeat 用 `CCU_WHILE` 包在**整个 transfer 之外**，循环体内仍保持「单个 `CCU_IF`
  包住 remote write 循环」的正确结构；
- **不要**把 `CCU_IF` 放进 per-peer 的 rank 循环内（这是之前单向丢数据的根因）。
