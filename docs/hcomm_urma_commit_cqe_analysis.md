# Hcomm URMA (V310) commit / cqe / sq / cq 组合排查

分析对象：`src/aicore/hcomm/impl/platform_v310/hcomm_aiv_urma.h`
配套结构：`src/aicore/hcomm/common/hcomm_inner_def.h`

## 0. 背景与约束（本次分析的前提）

- **约束1**：`commit=false` 的调用次数 ≤ `sqDepth`（用户设置值 D）。
- **约束2**：批量 commit 场景（一批 `commit=false` 后跟一个 `commit=true` 或 `Commit()`）
  **必须在最后一次且只在最后一次产生 cqe**（即整批只有一个 CQE）。
- **约束3**：`Drain` 之前必须先 `Commit`（或最后一次 `PostSend` 用 `commit=true`）。
  即不能攒一批 `commit=false` 后直接 `Drain`。保证 `Drain` 时 `cqCommitted == cqHead`，
  `PollCq(cqHead)` 等的全是已敲门铃、一定会到的 CQE，不会空转。
- `sqDepth` 用户可设，通信 API 对当前来说 `sqDepth <= cqDepth`。
- **实际物理容量 = 用户 sqDepth × 4**。结构体字段 `ubJfs.sqDepth` 存的就是物理值 **4D**。
- `sqDepth` 必为 2 的幂（故 4D 也是 2 的幂）。
- SQ 快满前必须 `PollCq` 回收，否则新 WQE 覆盖未消费数据 → 丢数据。
- CQ 快满前必须 `PollCq` 才能放下新 CQE。

记号：**C = 4D**（物理 SQ 环大小，BB 单位）。

## 1. 计数器与关键机制

`ChannelEntity`（`hcomm_inner_def.h:239-242`）：

| 字段 | 含义 | 步进 |
|---|---|---|
| `sqHead` | SQ 生产者，**BB 单位**，**32 位裸单调计数**（`hcomm_inner_def.h:239`，几乎不回绕，需 2^32 才回绕） | 每次 PostSend `+= wqeBbCnt`（`hcomm_aiv_urma.h:410`） |
| `sqTail` | SQ 消费者，**BB 单位，但只有 16 位有效**（来自硬件 `entryIdx:16`，`hcomm_inner_def.h:391`，到 65535 回绕到 0） | 仅 `PollCq` 里 `= cqeUb->entryIdx`（`:473`） |
| `cqHead` | 期望 CQE 数 | 仅 `config.cqe != 0` 时 `+1`（`:406-408`） |
| `cqTail` | 已消费 CQE 数 | `PollCq` 里推进（`:472`） |

- `wqeBbCnt`：`WRITE_WITH_NOTIFY / FAA / CAS = 2`，其余 = 1（`:386-389`）。
- SQ 门铃仅在 commit 敲：`CommitImpl → st_dev(sqHead, dbVa)`（`:333`）。未 commit，HW 不消费。
- **回收 SQ 的唯一途径是 `PollCq`**：它把 `sqTail` 推进到 `cqeUb->entryIdx`（`:473`）。
- `PollCq(expectIdx)` 的 `expectIdx` 语义是**目标 cqTail（CQE 计数域）**：`while (curTail != expectIdx)`（`:438`）。**不是 BB 域，也不是 sqHead 域。**

一个关键推论：**在约束2下，整批只有一个 CQE，其 `entryIdx` 指向批尾的 sqHead**，所以 poll 到这一个 CQE 就能一次性回收整批的所有 BB。

> **⚠️ 模数域不一致（关键）**：`sqHead` 是 32 位自然增长量，`sqTail` 只是硬件 `entryIdx` 的 **16 位低位镜像**。两者**不在同一模数域**，`sqHead - sqTail` 直接相减无意义。
> 例：`sqHead = 70000`（已绕过 2^16 一次），真实消费到 69990，但 `entryIdx` 上报 `69990 & 0xFFFF = 4454`，于是 `sqTail = 4454`。直接减得 `70000 - 4454 = 65546`（看似巨量 outstanding），而真实 outstanding 仅 10。
> **正确算 outstanding 必须先把 `sqHead` 也截到 16 位**：`outstanding = (uint16_t)((sqHead & 0xFFFF) - (sqTail & 0xFFFF))`，靠 16 位无符号回绕自动得到正确差值。
>
> **为什么截断不丢信息**：要的是"差值"。真实 `outstanding = sqHead - sqTail_true`（sqTail_true 是理想 32 位消费者位置），恒 ≤ 4D。硬件只给 `sqTail = sqTail_true & 0xFFFF`。在 mod 2^16 下：
> `(sqHead & 0xFFFF) - (sqTail & 0xFFFF) ≡ sqHead - sqTail_true = outstanding (mod 2^16)`。
> sqHead 被砍的高位与 sqTail 本就没有的高位在同余减法里对齐抵消。只要 `outstanding < 2^16`，就 `outstanding mod 2^16 == outstanding`，精确还原。
>
> **成立前提（本项目已满足）**：D 最大 8192 → 4D 最大 32768 = 2^15 < 2^16。这正是 wrap-safe 环形队列的经典要求「容量 ≤ 计数器模的一半」（`4D ≤ 2^16 / 2 = 32768`），恰好等号边界。cqDepth 默认 32768。
>
> **数值例子**（减法均按 uint16_t / mod 65536）：
> | # | sqHead | sqTail_true | 真实 outstanding | sqHead&0xFFFF | sqTail(=&0xFFFF) | (uint16_t)(H-T) | 结果 |
> |---|---|---|---|---|---|---|---|
> | 1 都没过边界 | 100 | 90 | 10 | 100 | 90 | 10 | ✓ |
> | 2 仅 sqHead 过边界 | 65540 | 65530 | 10 | 4 | 65530 | (4-65530)+65536=10 | ✓ |
> | 3 两者都过边界 | 70000 | 69990 | 10 | 4464 | 4454 | 10 | ✓ |
> | 4 满环(=4D) | 65540 | 32772 | 32768 | 4 | 32772 | (4-32772)+65536=32768 | ✓ |
> | 5 反证:若容量=2^16 | — | — | 65536 | — | — | 65536 mod 65536 = 0 | ✗ 与"空"混淆 |
>
> 例2/例4 里 sqHead 从 65540 被截成 4「看似变小」，但因 sqTail 也在 16 位域，减法回绕后仍得正确的 10 / 32768。例5 说明为何必须 `4D ≤ 32768`：若容量达 2^16，满(65536)与空(0)同余，无法区分——本项目 D≤8192 恰好避开。

## 2. 逐条结论（已按新背景修正）

### ✅ 非问题 1：约束1 在 bbcnt=2/混合下依然安全（原 BUG-4 撤销）

原担心：bbcnt=2 时 D 次调用写 2D 个 BB，超环覆盖。
**4D 容量正好吸收了这个 2 倍**：

- 一批 ≤ D 次调用，最坏全是 bbcnt=2 → 2D 个 BB ≤ 4D = C。**放得下，还有 2× 余量。**
- 混合调用同理，介于 D~2D BB 之间，均 ≤ 4D。

结论：约束1 用“次数”计，配合 4D 容量，对 bbcnt=1/2/混合都安全。
**注意**：安全裕度依赖 4× 这个系数。若哪天容量系数被改成 2×，bbcnt=2 满批就会踩到边界；1× 必然覆盖。这一点需在代码或设计文档里固化说明。

### ✅ 非问题 2：批量 CQ 溢出（原 BUG-5 撤销，前提是严格遵守约束2）

原担心：批量时 `cqeCnt` 一次跳 N，CQ 守卫的等值触发被跳过。
严格遵守约束2后，**整批只产 1 个 CQE，`cqHead` 每次只 `+1`**：

- CQ 守卫 `PollCqWhenSqOverflowCq`（`:302`）的等值条件 `(cqeCnt+10)%cqDepth == cqTail%cqDepth`，在 `cqeCnt` 单步 +1 时能可靠命中每个余数，不会被跨过。
- 且该函数的 poll 目标 `idx = min(cqeCnt, cqTail+100)` 完全在 **CQE 域**，与 `PollCq` 语义一致 → 域正确。

结论：**只要遵守约束2，CQ 溢出保护是对的**。
仅当用户违反约束2（批量中间也产 cqe，即“一批里一些产生 cqe”）时，`cqHead` 会一次跳多，等值触发可能被跨过 —— 但那是**用户违约**，不是本模块的 bug。

### ✅ 非问题 3：2 的幂假设成立（原 BUG-7 降级）

`sqeCtx->owner = (curHead & baseBlockCount) == 0 ? 1 : 0`（`:63-64`），`baseBlockCount = 4D = 2^m`。
- `curHead & 2^m` 取的是 curHead 的第 m 位，等价于 `(curHead / 2^m) & 1`，即“第几圈”的奇偶 → owner 每绕环一圈翻转一次。**在 2 的幂前提下正确。**
- `PollCq` 寻址 `curTail & (cqDepth-1)`（`:439`）、`curHead % baseBlockCount`（`:382/392`）也都依赖 2 的幂 → 成立。

结论：owner 与寻址在“2 的幂保证”下正确，不是 bug。
**遗留**：无运行期校验。若上层误传非 2 的幂（尤其 cqDepth），会静默错乱。建议加 assert/校验（见 §4）。

### ✅ BUG-A（原 BUG-8）：编译阻断 —— 已修复

已确认修复（2026-08-18 复核）：
- `hcomm_aiv_urma_def.h:125-128` 现声明了 `PollCqWhenSqOverflowCq` 与 `PollCqWhenSqOverflowSq` 两个成员，与 `.h` 中的实现一一对应，原来那个不存在的 `PollCqWhenSqOverflow` 声明已移除。
- `PollCqWhenSqOverflowSq`（`hcomm_aiv_urma.h:311-323`）已删除引用作用域外 `cqTail`/`cqDepth` 的 `KERNEL_LOG`。编译阻断解除。

### ✅ BUG-B（原 BUG-1）：SQ 守卫读错字段 —— 已随 BUG-A 修复

已确认修复：`hcomm_aiv_urma.h:315` 现为
```cpp
uint32_t sqTail = channelEntity->sqTail;   // 已改读正确字段（BB 域消费者）
```
不再误读 `cqTail`。

### ✅ BUG-C（原 BUG-2）：SQ 守卫 poll 目标算错域 —— 已修复

修复后 `PollCqWhenSqOverflowSq`（`hcomm_aiv_urma.h:326-330`）把 poll 目标改到 **CQE 域**朝 `cqHead(=cqeCnt)` 推进，且仅在 `cqeCnt != cqTail`（有未消费 CQE）时才 poll：
```cpp
if (cqeCnt != cqTail) {
    uint32_t idx = (cqTail + NUM_CQE_PER_POLL_CQ) > cqeCnt ? cqeCnt : cqTail + NUM_CQE_PER_POLL_CQ;
    (void)PollCq(channel, idx);            // idx 在 CQE 域，不再是 BB 域
} else {
    KERNEL_LOG(KERNEL_ERROR, "...SQ near full but no outstanding CQE to reclaim...");
}
```
不再把 BB 域的 idx 当 CQE 计数用，也不会等一个永不到来的 CQE 空转 1e6。

### ✅ BUG-D（原 BUG-3，仅 SQ 侧）：等值触发被 bbcnt=2 跨过 —— 已修复

修复后触发改为 **16 位域差值 + `>=`**（`hcomm_aiv_urma.h:325`）：
```cpp
uint16_t outstanding = (uint16_t)((uint16_t)(sqHead & 0xFFFFU) - (uint16_t)(sqTail & 0xFFFFU));
if ((uint32_t)outstanding + POLL_CQ_THRESHOLD >= sqDepth) { ... }
```
`>=` 不会被 bbcnt=2 的 sqHead 跳步跨过；16 位域相减规避了 sqHead(32 位)/sqTail(16 位)域不一致。

### ⚠️ BUG-C/D 修复后的残留局限（新增，重要）

守卫是 `void` 返回，`PostSend`（`hcomm_aiv_urma.h:373`）**忽略其结果**。因此当命中 `else` 分支（无 CQE 可回收）时，只打错误日志，**随后 PostSend 照样写、照样覆盖**。即：
- 守卫能修好「该回收却因域错/触发跨过而回收不了、或空转」的问题；
- 但修不了「**根本没有 completion 可依赖**」这一根因 —— 那是 BUG-E（cqe 全关）和用户违约（cqe 太稀疏，outstanding 追上唯一的旧 CQE 之前就超 4D）的领域。
若要真正阻止覆盖，需让守卫返回状态、`PostSend` 在无法回收时**阻塞/报错而非继续写**（见 §4 增补）。

### ❌ BUG-G（新发现，重要）：守卫用 post-time 的 `cqHead` 作 poll 目标，会等到"未敲门铃的 CQE"

根因：`cqHead`（即传入守卫的 `cqeCnt`）在 `PostSend` **post 时**就 `++`（`hcomm_aiv_urma.h:406-409`，只看 `config.cqe`，**完全不看是否 commit/敲门铃**）；而门铃只在 `CommitImpl`（`:333`）敲。因此 `cqeCnt` 计的是"打算产生的 CQE"，其中可能包含**尚未敲门铃、HW 根本没被通知去产生**的 CQE。守卫（修复后）把 `cqeCnt` 当作 poll 目标上限，就会让 `PollCq` 去等一批不存在的 CQE。

**主复现（命中主流批量 commit，cqe 每次都产生，未违反约束2；D=8→4D=32，cqDepth=32768，plain WRITE bbcnt=1）**：

| 阶段 | 动作 | post 后 sqHead | post 后 cqHead | 门铃(db)敲到 | cqTail |
|---|---|---|---|---|---|
| 1–7 | cqe=1, commit=false | 7 | 7 | 0（未敲） | 0 |
| 8 | cqe=1, commit=true | 8 | 8 | **8** | 0 |
| 9–15 | cqe=1, commit=false | 15 | 15 | 8 | 0 |
| 16 | cqe=1, commit=true | 16 | 16 | **16** | 0 |
| 17–21 | cqe=1, commit=false | 21 | 21 | 16 | 0 |
| 22 | cqe=1, commit=false | 22 | 22 | 16 | 0 |

- 发第 23 个时守卫用快照：`curHead=22, cqeCnt=22`。触发：`outstanding=22, 22+10=32>=32`。
- poll：`idx = min(cqTail+100, cqeCnt) = min(100, 22) = 22` → `PollCq(channel, 22)` 要消费 **22** 个 CQE。
- **但门铃只敲到 sqHead=16，HW 只处理 WQE 0–15，只会产生 16 个 CQE**。第 17–22 个 CQE 对应的 WQE（17–21 那批）门铃没敲、HW 未被通知 → 不存在。
- `PollCq` 卡在第 17 个（curTail=16）的 owner 位轮询，空转到 `HCOMM_URMA_MAX_RETRY_TIMES=1e6`（`:447`）超时 → SQ 未回收 → PostSend 照写 → 覆盖。「等到 cqeCnt=22，实际只等得到 16。」

> **这个例子的分量**：cqe 每次都产生（最密），**完全没违反约束2**，用的只是最普通的**批量 commit（doorbell batching：攒几个 commit=false 再敲一次门铃）** —— 主流用法。说明 BUG-G 命中的是常规批量 commit，不是边角违约。

**扩展案例（验证修复在各维度都成立）**：

- **扩展1｜bbcnt=2（notify/FAA/CAS），批量 commit**：D=8→4D=32，每次 cqe=1。发 1–4（commit=false，每次 +2 BB）→ sqHead=8, cqHead=4；第 5 次 commit=true → 门铃敲到 sqHead=10（第5个也+2）；再发 6–13（commit=false）→ sqHead=26, cqHead=13。发第 14 个前守卫：outstanding=26, 26+10≥32 触发。
  - 修复前：`idx=min(100,cqHead=13)=13`，但门铃只敲到 sqHead=10（对应 5 个 CQE）→ 等 13 个只来 5 个 → 空转。
  - 修复后(方案B)：SQ 模式 `PollCq<true>` 读 CQE0-4(entryIdx=2/4/6/8/10),sqTail 推进到 10,outstanding=26-10=16 < 22 安全 → 停在第 5 个,不去读未敲门铃的第 6 个 → ✅ 无空转。
  - 说明 16 位域差值（BUG-D 修复）与 entryIdx 停止条件（BUG-G 方案B）**对 bbcnt=2 叠加正确**。

- **扩展2｜超 cqDepth 方向**：cqDepth 默认 32768 = 4D_max。因每 WQE 至多 1 CQE、outstanding CQE ≤ outstanding WQE ≤ 4D ≤ cqDepth，**只要 cqDepth ≥ 4D，CQ 永不先于 SQ 溢出**；且 CQ 守卫 `PollCqWhenSqOverflowCq` 在门铃后调用，用 `cqeCnt`（此刻已敲门铃）安全。故"超 cqDepth"在契约内不可达，不受 BUG-G 影响。

- **扩展3｜未 commit 就逼近 SQ 满（违反约束1）**：一直 commit=false 攒到 outstanding≥4D−10。这批门铃没敲,已敲门铃的已被 poll 完(sqTail 追平门铃位) → SQ 模式提前判断"已安全"返回(零成本,等价方案A 的 cqCommitted==cqTail)。但只打日志,PostSend 仍写 → 覆盖。这属约束1 违约,且是「残留」要根治的（守卫返回状态 + PostSend 阻塞/报错）。**注:方案B 在此不空转**(提前返回),方案A 走 else 也不空转。

- **扩展4｜Drain 前未 commit（违反约束3）**：约束3（Drain 前必先 commit）**由用户行为保证**,`Drain`（`:593`）**不做额外 commit**。遵守时所有 post 的 CQE 都已敲门铃,`PollCq(cqHead)` 无空转 → ✅；若违约（攒 commit=false 直接 Drain）,最后一批门铃没敲,`PollCq(cqHead)` 会等未产生的 CQE → **空转 1e6 超时,属用户责任**。

原其它同源触发（修复前）：cqe 放大批中间整段最后才 commit（违反约束2）；批内纯 cqe=0 逼近 4D。修复后前者靠 entryIdx 停止条件不再空转,后者仍需「残留」兜底。

共性根因：**守卫曾依赖 `cqeCnt` 作为"即将到来的 CQE"的代理，而 `cqeCnt` 在 post 时就 ++、门铃在 commit 才敲，二者脱节**。修复（§4「BUG-G」方案B 已落地）：SQ 守卫改调 PollCq SQ 安全模式,按 entryIdx 重算 outstanding、一旦安全立刻停,天然不读未敲门铃 CQE;`cqeCnt` 回归"意图计数"本职。

### ❌ BUG-E（原 BUG-6）：cqe 全关时无法回收 SQ（设计缺口）

`config.cqe == 0`（全程不产生 cqe）时 `cqHead` 恒为 0：
- 没有任何 completion → `sqTail` 永不推进 → **SQ 无从回收**。
- `Drain`/`Commit` 走到 `PollCq(cqHead=0)`，`expectIdx==0` 直接返回（`:422`）→ 什么都不做。
- 叠加 BUG-C：守卫一旦触发还会空转 1e6 次。

安全边界：单批 ≤ 2D BB < 4D，**单批内不会溢出**；但跨多批持续 `cqe=0` 且不 drain，`sqHead` 无限增长绕环 → 覆盖。
这一格（**不产生 cqe + 超 4D outstanding**）在当前逻辑下**必然出问题**，且无 CQE 可依赖，属于设计层面缺口，需靠契约或强制补 cqe 解决（见 §4）。

### ⚠️ BUG-F（新发现，潜在，低置信）：2-BB WQE 跨环尾拆分时 owner 位错

`:391-398` 显式处理了 bbcnt=2 的 WQE 落在环尾（`curHead % 4D == 4D-1`）时拆成两半：前半写末槽、后半写 0 槽。但 owner 位在 `HcommUrmaFillSqeCtx` 里**只按 `curHead` 算了一次**，两个 BB 共用同一 owner。而 0 槽属于**下一圈**，owner 应取反。HW 若按 BB owner 判有效性，跨环尾的第二个 BB owner 会错。
需确认 HW 对拆分 2-BB WQE 的 owner 语义；若确有影响，需按 BB 分别写 owner。

## 3. 组合矩阵（是否依赖“自动回收”决定成败）
## 3. 组合矩阵（全场景确认,方案B 最终实现）

维度：commit 模式 × cqe 产生方式 × bbcnt × 是否超 4D(SQ) × 是否超 cqDepth(CQ)。

当前 bug 状态（本表据此判定）：BUG-A/B/C/D/G ✅**均已修**（SQ 守卫重写 + PollCq 模板参数/entryIdx 停止,方案B,**提前判断已移至守卫外层if**）；**BUG-E 设计缺口未处理**；守卫仍是 `void` 返回、`PostSend` 忽略结果（残留局限,仅影响"无committed CQE 可回收"时且会空转 1e6）；约束1/2/3 违约均由**用户/契约保证**（Drain 不做额外 commit）；BUG-F 待复核。

前置事实：
- 一个 WQE 最多产 1 个 CQE，最大 outstanding CQE ≤ 最大 outstanding WQE ≤ 4D。**只要物理 `cqDepth ≥ 4D`（默认 32768 = 4×8192），CQ 永不先于 SQ 溢出** → "超 cqDepth"在契约内不可达（除非 SQ 已溢出，或 cqDepth<4D 违约）。故下表 CQ 列多为"不可达/安全"。
- SQ 自动回收现状（三处修复叠加,方案B 最终形态）：①**触发** = 16 位域差值 `outstanding+10 >= sqDepth`（BUG-D,bbcnt=2 不跨过）；②**poll** 在 CQE 域（BUG-C）；③**停止** = entryIdx 推导的 outstanding 达安全线（BUG-G 方案B,**守卫外层if保证不安全才调PollCq**,PollCq 内每读一个 CQE 就用 entryIdx 重算,一旦 `outstanding+10 < sqDepth` 立刻停,天然不读未敲门铃 CQE）。**只要有"已提交且未消费"的 CQE，超 4D 都能自动回收。**

**全维度遍历**（按根因归并,2×4×3×2=48种组合归并为11类）:

| # | commit 模式 | cqe 产生 | bbcnt | 不超 4D | 超 4D(SQ) | 超 cqDepth | 根因/说明 |
|---|---|---|---|---|---|---|---|
| 1 | 非批量(恒 true) | 全产生 | 1 | ✅ | ✅ | ⛔不可达 | 契约内标准:每次commit门铃同步,超4D有充足committed CQE回收 |
| 2 | 非批量 | 全产生 | 2/混合 | ✅ | ✅ | ⛔不可达 | 16位域触发+entryIdx停止对bbcnt=2有效(扩展1验证) |
| 3 | 非批量 | 部分产生 | 任意 | ✅ | ✅ | ⛔不可达 | commit=true故已敲门铃;cqe密度正常时回收追得上 |
| 4 | 非批量 | 全不产生(cqe=0) | 任意 | ✅(总BB<4D) | ❌ **BUG-E** | — 无CQE | 无CQE可poll,空转1e6→超时→PostSend仍写→覆盖(设计缺口) |
| 5 | 批量(合约) | 末尾 1 个 | 1 | ✅(单批≤D<4D) | ✅ | ⛔不可达 | 跨批前批CQE已committed,poll释放整批(entryIdx=批尾) |
| 6 | 批量(合约) | 末尾 1 个 | 2/混合 | ✅(单批≤2D<4D) | ✅ | ⛔不可达 | 扩展1已验证bbcnt=2 + entryIdx停止正确叠加 |
| 7 | 批量 | 全产生(每批末尾commit) | 任意 | ✅ | ✅ | ⛔不可达 | **主复现场景修复点**:entryIdx停止条件根除空转(BUG-G已修) |
| 8 | 批量 | 全不产生(cqe=0) | 任意 | ✅(总BB<4D) | ❌ **BUG-E** | — 无CQE | 同#4(设计缺口) |
| 9 | 批量(**违约2**:批中多cqe) | 多个 | 任意 | ⚠️视BB量 | ⚠️ | ❌ | SQ侧✅(朝committed poll无恙);CQ侧❌(expectIdx被跨过),**用户违约** |
| 10 | 任意(**违约1**:未commit攒近满) | 任意 | 任意 | — | ❌ | — | 无committed CQE,空转1e6→超时→覆盖,**用户违约** |
| 11 | Drain(**约束3**) | 任意 | 任意 | — | 遵守✅ / 违约❌ | — | 遵守(drain前commit):PollCq按expectIdx无空转;违约:空转1e6,**用户契约** |

> **"稀疏"注（#3 的边界）**：修复后(方案B)SQ 模式按 entryIdx 重算 outstanding,一旦安全立刻停。只要"已提交未消费的 CQE"的最新 `entryIdx` 能把 `sqTail` 抬到让 `outstanding < sqDepth-10`，就安全。失败仅当**根本没有已提交 CQE 可回收**（撞未敲门铃边界 → 空转 1e6 超时）——即 BUG-E（#4/#8,全不产生）或违约1（#10,未 commit）。**不再有"cqe 稀疏但已 commit 却空转"的情况**（那是 BUG-G,已修）。#3 若cqe极度稀疏(如每100个WQE才1个cqe),outstanding可能降不到安全线,但这属cqe密度设计问题,非bug。

**一句话总结（BUG-G 方案B 最终实现,移除PollCq内提前判断后）**：
- **契约内(遵守约束1/2/3)的所有组合都安全**（#1-3/#5-7 全 ✅）：不超 4D 本就安全(守卫不触发)；超 4D 时只要有已提交 CQE,守卫经 16 位域触发、按 entryIdx 重算 outstanding 一旦安全立刻停、经 entryIdx 释放 SQ——主流批量 commit（#7）的空转已被 entryIdx 停止条件根除。
- **仍会覆盖的唯一根因是"没有已提交 CQE 可依赖"**：①cqe 全关（BUG-E，#4/#8）；②违约1 未 commit 攒到近满（#10）。二者共性——守卫触发→PollCq→撞未敲门铃边界→空转1e6超时返回→`PostSend` 忽略返回仍写→覆盖。根治见 §4「残留增补」（守卫返回状态 + PostSend 阻塞/报错）。**方案B 在契约内不空转(停止条件在读之前判,守卫外层if已筛掉"已安全"情况);BUG-E/违约1 空转 1e6 超时,但不阻止覆盖(取舍:无跨端ABI,代价是错误场景空转几毫秒)。**
- **约束违约均属用户责任**：违约1（#10 未 commit 攒近满,空转1e6）、违约2（#9 批中多 cqe,CQ侧被跨过）、违约3（#11 drain 前未 commit,空转 1e6）都由契约/用户保证；`Drain` 按约定不做额外 commit。
- **CQ 溢出**在契约内（cqDepth≥4D）不可达且守卫域正确。
- 另：BUG-F（跨环尾 2-BB WQE owner 拆分）待 HW 语义复核。
## 4. 解决方案

### 修 BUG-A（编译）
`hcomm_aiv_urma_def.h` 用两个真实函数替换 `PollCqWhenSqOverflow` 声明：
```cpp
__aicore__ inline void PollCqWhenSqOverflowCq(
    ChannelHandle channel, const SqContext& sqCtx, const CqContext& cqCtx, uint32_t sqHead, uint32_t cqeCnt);
__aicore__ inline void PollCqWhenSqOverflowSq(
    ChannelHandle channel, const SqContext& sqCtx, const CqContext& cqCtx, uint32_t sqHead, uint32_t cqeCnt);
```
并修正 `PollCqWhenSqOverflowSq` 的 `KERNEL_LOG`，只用在作用域内的量（`cqDepth` 可由传入的 `cqCtx` 取，`cqTail` 由 `channelEntity` 取）。

### 修 BUG-B/C/D（重写 SQ 溢出守卫）—— ✅ 已落地
核心：**触发判断在 16 位 BB 域用差值 `>=`；回收 poll 在 CQE 域朝 `cqHead` 推进**。已应用到 `hcomm_aiv_urma.h:311-337`。
```cpp
__aicore__ inline void HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCqWhenSqOverflowSq(
    ChannelHandle channel, const SqContext& sqCtx, const CqContext& cqCtx, uint32_t sqHead, uint32_t cqeCnt)
{
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    uint32_t sqTail  = channelEntity->sqTail;                 // BUG-B: 读对字段（BB 域消费者，仅 16 位有效）
    uint32_t cqTail  = channelEntity->cqTail;
    uint32_t sqDepth = sqCtx.contextInfo.ubJfs.sqDepth;       // = 4D，物理环大小（2 的幂，≤ 2^16）
    uint32_t cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;

    // BUG-D: 差值判断（>=，bbcnt=2 也不会被跨过）
    // 关键：sqHead 是 32 位自然增长，sqTail 只有 16 位有效（来自 entryIdx:16），
    // 二者模数域不同，必须都截到 16 位再做无符号减法，靠 16 位回绕得到真实 outstanding。
    // 正确前提：真实 outstanding ≤ 4D ≤ 32768 = 2^15 < 2^16（本项目 D≤8192 保证）。
    uint16_t outstanding = (uint16_t)((uint16_t)(sqHead & 0xFFFFu) - (uint16_t)(sqTail & 0xFFFFu));
    if ((uint32_t)outstanding + POLL_CQ_THRESHOLD >= sqDepth) {
        // BUG-C: poll 目标在 CQE 域，朝 cqHead(=cqeCnt) 方向推进；
        // poll 到的 CQE 通过 entryIdx 抬升 sqTail，从而释放 BB。
        // 约束2 下每批 1 个 CQE，其 entryIdx = 批尾 sqHead，poll 一个即释放整批。
        if (cqeCnt != cqTail) {   // 有未消费 CQE 才 poll，避免 expectIdx 落到永不到来的值
            uint32_t idx = (cqTail + NUM_CQE_PER_POLL_CQ) > cqeCnt ? cqeCnt : cqTail + NUM_CQE_PER_POLL_CQ;
            (void)PollCq(channel, idx);
        } else {
            KERNEL_LOG(KERNEL_ERROR,
                "Hcomm URMA SQ near full but no outstanding CQE to reclaim: sqHead=%u sqTail=%u cqeCnt=%u\n",
                sqHead, sqTail, cqeCnt);   // 命中即说明用户违约(cqe 太稀疏)或 BUG-E
        }
    }
}
```
要点：
- **outstanding 必须在 16 位域内算**，绝不能直接 `sqHead - sqTail`（32 位 vs 16 位，域不一致，见 §1）。✅ 已按此实现。
- 阈值 `POLL_CQ_THRESHOLD` 保持提前量，避免刚好写满才 poll。✅

**残留增补（尚未落地，建议下一步）**：当前守卫 `void` 返回、`PostSend`（`:373`）忽略结果，`else` 分支只打日志，随后**仍会写 → 覆盖**。建议：
- 让守卫返回是否已把 outstanding 降到安全线；单次 poll 不够时**循环 poll** 到 `(uint16_t)((sqHead & 0xFFFF) - (channelEntity->sqTail & 0xFFFF)) + THRESHOLD < sqDepth`（每轮重读 `sqTail`，前提是有 CQE 可消费）。
- 若确无 CQE 可消费（`cqeCnt == cqTail` 仍不满足安全线），`PostSend` 应**阻塞/返回错误而非继续写**，从根上杜绝覆盖（覆盖 BUG-E 与 cqe 过稀疏两种情况）。

### 处理 BUG-G（poll 目标应是"已敲门铃的 CQE 数"，而非 post-time 的 cqHead）—— ✅ 已落地(方案B)

核心问题：`cqHead`(=`cqeCnt`)在 post 时就 ++、门铃在 commit 才敲,守卫若朝 `cqHead` poll,会等未敲门铃、不会到来的 CQE → 空转。

#### 方案A：新增 `cqCommitted` 字段(未采用,保留供对比)

新增一个**只在敲门铃时推进**的计数,守卫只朝它 poll。设计:
- `ChannelEntity`(`hcomm_inner_def.h`)加 `uint32_t cqCommitted;`,从 `reserve[144]` 切 4 字节(`reserve[140]`),总大小仍 256(ABI 不变)。
- `CommitImpl` 敲门铃(`:333` `st_dev(sqHead, dbVa)`)后写 `channelEntity->cqCommitted = cqeCnt;`。
- `PollCqWhenSqOverflowSq` poll 上限由 `cqeCnt` 改为 `cqCommitted`:
```cpp
uint32_t cqCommitted = channelEntity->cqCommitted;   // 只含已敲门铃、HW 一定会产生的 CQE
if (cqCommitted != cqTail) {
    uint32_t idx = (cqTail + NUM_CQE_PER_POLL_CQ) > cqCommitted ? cqCommitted : cqTail + NUM_CQE_PER_POLL_CQ;
    (void)PollCq(channel, idx);
} else {
    // 已提交 CQE 全部 poll 完仍不够 → 确无可回收(BUG-E/违约1);打日志,不空转
}
```

优点:
- 契约内(约束1/2)与 BUG-E/违约1 都**不空转**(零成本发现:cqCommitted==cqTail 直接走 else 打日志)。
- 语义清晰:`cqCommitted` 显式表达"已敲门铃的 CQE 数"。

缺点:
- 需跨端 ABI 字段(host 侧若有对应结构需同步)。
- 三处维护(定义/写入/读取)。

#### 方案B：PollCq 模板参数 + entryIdx 停止条件 ✅ 已落地

用 `if constexpr(sqSafeMode)` 在 PollCq 内区分停止条件,守卫调用"SQ 安全模式":每读一个 CQE 就用 entryIdx 重算 outstanding,一旦安全立刻停,天然不去读未敲门铃 CQE。已应用:

- **`hcomm_aiv_urma_def.h:129`** PollCq 签名加模板参数:
```cpp
template <bool sqSafeMode = false>
__aicore__ inline uint32_t PollCq(
    ChannelHandle channel, uint32_t expectIdx, uint32_t sqHead = 0, uint32_t sqDepth = 0, uint32_t threshold = 0);
```

- **`hcomm_aiv_urma.h:442-520`** PollCq 实现(核心逻辑):
```cpp
template <bool sqSafeMode>
__aicore__ inline uint32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCq(...) {
    // SQ 安全模式:提前判断,已安全则不 poll(零成本早退,等价方案A的 cqCommitted==cqTail)
    uint32_t sqTail = channelEntity->sqTail;
    if constexpr (sqSafeMode) {
        uint16_t outstanding = (uint16_t)((uint16_t)(sqHead & 0xFFFFU) - (uint16_t)(sqTail & 0xFFFFU));
        if ((uint32_t)outstanding + threshold < sqDepth) {
            return HCOMM_SUCCESS;
        }
    }

    // 停止条件 lambda(if constexpr 编译期选分支,无运行期开销)
    auto shouldContinue = [&]() -> bool {
        if constexpr (sqSafeMode) {
            uint16_t outstanding = (uint16_t)((uint16_t)(sqHead & 0xFFFFU) - (uint16_t)(sqTail & 0xFFFFU));
            return (uint32_t)outstanding + threshold >= sqDepth; // 不安全 → 继续 poll
        } else {
            return curTail != expectIdx; // 原逻辑:Drain/CQ 守卫按 expectIdx
        }
    };

    while (shouldContinue()) {
        // ... owner 位校验、status 检查(与原 PollCq 完全相同)...
        curTail++;
        if constexpr (sqSafeMode) {
            sqTail = cqeUb->entryIdx; // 每次更新 sqTail,循环顶部重算 outstanding 判停止
        }
    }
    // update & doorbell(原逻辑)
}
```

- **`hcomm_aiv_urma.h:311-331`** PollCqWhenSqOverflowSq 守卫改调 SQ 安全模式:
```cpp
uint16_t outstanding = (uint16_t)((uint16_t)(sqHead & 0xFFFFU) - (uint16_t)(sqTail & 0xFFFFU));
if ((uint32_t)outstanding + POLL_CQ_THRESHOLD >= sqDepth) {
    constexpr uint32_t dummyExpectIdx = 0xFFFFFFFFU; // sqSafeMode 下不用 expectIdx,传虚值
    (void)PollCq<true>(channel, dummyExpectIdx, sqHead, sqDepth, POLL_CQ_THRESHOLD);
}
```

- **`hcomm_inner_def.h:243`** 删除 `cqCommitted`,`reserve` 从 140 回到 144(无跨端 ABI 变动)。
- **`hcomm_aiv_urma.h:338`** CommitImpl 删除 `cqCommitted = cqeCnt` 写入。
- **Drain/CQ 守卫不变**:仍调 `PollCq(channel, expectIdx)`(sqSafeMode=false 默认)。

优点:
- **无跨端 ABI 字段**,无需 host 侧同步(reserve 完整)。
- 代码复用:PollCq 内 if constexpr 仅 ~10 行额外逻辑,owner 校验/status 检查等主体代码完全共享。
- 契约内(约束1/2)**不空转**:停止条件在读之前判,达标即退,天然停在已敲门铃边界。

缺点:
- BUG-E(cqe=0)或违约1(未 commit 攒近满)时,**会空转 1e6 次再超时**(方案A 是零成本日志)。取舍:这两类本就是错误场景,空转代价可接受(1e6×owner 位读 ≈ 几毫秒级);且空转后仍返回超时错误,不影响正确性。

#### 两方案对比

| | 方案A(cqCommitted) | 方案B(entryIdx 停止,已落地 ✅) |
|---|---|---|
| 契约内(约束1/2) | ✅ 无空转 | ✅ 无空转 |
| BUG-E/违约1 | ✅ 零成本日志 | ❌ 空转 1e6 超时 |
| 跨端 ABI | ❌ 需 host 同步 | ✅ 无变动 |
| 代码复杂度 | 需三处维护 | PollCq 内 if constexpr(~10 行) |
| 语义清晰度 | 显式字段 | 隐式(停止条件推导) |

**选型理由**:方案B 避免跨端 ABI 同步风险,且严格守约束时性能一致;BUG-E/违约1 的空转仅影响错误场景,可接受。若后续 host 侧无障碍,可切回方案A 获零成本错误检测。

用主复现例验证(D=8, 4D=32, 门铃敲到 16, sqHead=22):
SQ 模式 PollCq:sqTail=0 不安全 → 读 CQE0(entryIdx=1)→ sqTail=1, outstanding=21 仍不安全 → 读 CQE1…一路到 CQE15(entryIdx=16)→ sqTail=16, outstanding=6 < 22 → **停,在读 CQE16(未敲门铃)之前退出**。✅ 无空转。

用主复现例验证(D=8, 4D=32, 门铃敲到 16, sqHead=22):
SQ 模式 PollCq:sqTail=0 不安全 → 读 CQE0(entryIdx=1)→ sqTail=1, outstanding=21 仍不安全 → 读 CQE1…一路到 CQE15(entryIdx=16)→ sqTail=16, outstanding=6 < 22 → **停,在读 CQE16(未敲门铃)之前退出**。✅ 无空转。

**三计数语义（方案B 下不再需要 cqCommitted,仅两个核心计数）**：

| 计数 | 语义 | 用途 |
|---|---|---|
| `cqHead`/`cqeCnt` | 已 **post**（意图产生） | ①per-cqe `++`（`:428-430`）②`Drain` 排空目标（须先 commit,见约束3） |
| `cqTail` | 已 **消费** | `PollCq` 推进 |
| `sqTail` | **已消费 WQE 的 SQ 索引**(来自 CQE entryIdx:16) | 用于算 outstanding(16 位域差值,见 BUG-D);SQ 模式每次更新,停止条件依赖它 |

方案A 的 `cqCommitted`(已敲门铃数)在方案B 中被**隐式推导**:SQ 模式每读一个 CQE 就更新 sqTail=entryIdx,再算 outstanding,达标即停 → 天然停在已敲门铃边界,无需显式字段。

**约束3 与 Drain**：`Drain`（`:593`）`PollCq(cqHead)` 语义上要排空"全部已 post"。若攒一批 `commit=false` 后直接 `Drain`,最后一批门铃没敲,`PollCq(cqHead)` 会等空转(与 BUG-G 同族)。**约束3（Drain 前必先 commit）由用户行为保证**,遵守时所有 post 的 CQE 都已敲门铃,`PollCq(cqHead)` 无空转。按约定 **`Drain` 内不做额外 commit**（不检测、不自动补 commit）;违约的后果由用户承担。

### 处理 BUG-E（cqe 全关无回收）
二选一，建议 (a)：
- **(a) 契约固化**：文档 + 运行期校验 —— `config.cqe==0` 时禁止 outstanding BB 超过 4D；`Drain/Commit` 在 cqe==0 时对回收无效，需上层保证不超容量。命中风险点打 `KERNEL_ERROR`。
- **(b) 强制补 cqe**：当检测到 outstanding 接近 4D 时，对下一个/环尾 WQE 强制置 `cqe=1`，制造一个回收点。改动更大，且与“用户显式不要 cqe”的语义冲突，谨慎采用。

### 处理非问题1/3 的加固（防回归）
- 在 Init/建链路径加校验：`sqDepth`(4D) 与 `cqDepth` 均为 2 的幂、且 `cqDepth >= sqDepth(4D)`；容量系数（4×）以具名常量表达，避免被误改。

### 处理 BUG-F（潜在）
确认 HW 对跨环尾拆分的 2-BB WQE 的 owner 语义。若 HW 逐 BB 校验 owner，则在 `:391-398` 拆分分支里，对写入 0 槽的后半 BB 单独按“下一圈”奇偶写 owner（`(sqHead+1) & 4D` 对应位取反）。

## 5. 建议修复顺序

1. ~~BUG-A（编译阻断）~~ **已修复**（2026-08-18）。
2. ~~BUG-B（SQ 守卫读错字段）~~ **已随 BUG-A 修复**（`:315` 现读 `sqTail`）。
3. ~~BUG-C + BUG-D（重写 SQ 溢出守卫）~~ **已修复**（16 位域差值 `>=` 触发 + CQE 域 poll）。
4. ~~BUG-G（poll 目标改为"已敲门铃的 CQE"）~~ **已修复(方案B)**：PollCq 加模板参数 `sqSafeMode`,SQ 守卫调 `PollCq<true>`,按 entryIdx 重算 outstanding、一旦安全立刻停,天然不读未敲门铃 CQE。无跨端 ABI 变动。方案A(cqCommitted 字段)保留供对比,见 §4。
5. **残留（下一步）**：守卫 `void` 返回、`PostSend` 忽略结果——无已提交 CQE 可回收时仍会写覆盖（BUG-E / 违约1）。让守卫返回状态，`PostSend` 无法回收时阻塞/报错。见 §4「残留增补」。
6. BUG-E 的契约/校验（覆盖 cqe 全关的空缺）——与第 5 项同源，一并解决。
7. 非问题1/3 的加固校验（2 的幂、cqDepth≥4D、**4D≤32768 即 D≤8192**、4× 常量化）。
8. BUG-F 复核（依赖 HW owner 语义确认）。

## 6. 专题：约束1 放宽到"调用次数 ≤ 4D"的全场景遍历

**前提**：本章假设把约束1 从"`commit=false` 调用次数 ≤ D"放宽为"`commit=false` **调用次数** ≤ 4D"（用户已知 4D、想吃满物理容量）。其余约束（2/3）与已落地的 BUG-A/B/C/D/G 修复不变。记 C = 4D = 物理环 BB 数。

### 6.1 三个先决事实

1. **覆盖判据**：环内"未消费 BB"（committed-未消费 + uncommitted 全部占坑）达到 **C=4D** 时，下一个 BB 覆盖最旧的未消费 BB → 丢数据。安全条件是 **outstanding < 4D**（还需留守卫阈值）。
2. **守卫触发线**：`outstanding + 10 >= 4D`，即到 **4D−10** 触发。触发后只能回收**已敲门铃**（cqCommitted）的部分；**未提交批中途回收不了自身**。
3. **次数 ≠ BB 数**：`outstanding` 按 BB 累加，而"调用次数"经 bbcnt 放大：bbcnt=1 → 次数=BB 数；bbcnt=2 → BB 数 = 2×次数；混合介于其间。

### 6.2 致命点：按"次数 ≤ 4D"算，bbcnt=2 直接翻倍超容量

| bbcnt | 4D 次调用产生的 BB 数 | 对比容量 4D | 结论 |
|---|---|---|---|
| 1 | 4D | = 4D | 恰好填满，**零裕度**（见 6.3） |
| 2（notify/FAA/CAS） | **8D** | = 2×容量 | ❌ **必覆盖** |
| 混合 | 4D ~ 8D | ≥ 容量 | ❌ 超出部分覆盖 |

所以"次数 ≤ 4D"对 bbcnt=2/混合**根本不成立**。若要按 4D 口径，必须改成按 **ΣwqeBbCnt**（BB 数）计，而非次数。

### 6.3 即使 bbcnt=1，"正好 4D"也危险

- **零裕度**：uncommitted 批填到 4D 时，守卫在 4D−10 已触发，但本批没 commit → `cqCommitted==cqTail` → 走 else → 只打日志，`PostSend` 仍写完这批（正好写满第 4D 个）。**只要不再多发即不覆盖，但没有任何缓冲**：任何 off-by-one、或阈值内的 backlog，都会越过 4D。
- **与已提交 backlog 叠加**：`outstanding = backlog(committed-未消费) + 本批 uncommitted`。若发本批前存在未回收的 backlog，本批 4D BB + backlog > 4D → **覆盖**。而守卫在本批进行中只能回收 backlog（若其有 cqe 且已 commit）；backlog 若无 cqe 或未被 poll，则腾不动。

### 6.4 全场景遍历（约束1 = 次数 ≤ 4D）

维度：commit 模式 × cqe 产生 × bbcnt × 有无已提交 backlog。列出是否覆盖。

统一基准：**D=8 → 4D=32**（环容量 32 BB），阈值 POLL_CQ_THRESHOLD=10（守卫在 outstanding≥22 触发）。"次数"指 `commit=false` 的连续调用次数。

| # | 场景 | bbcnt | 具体例子（D=8, 4D=32, 环容量 32 BB） | 哪次出问题 | 结果 |
|---|---|---|---|---|---|
| 1 | 单批 commit=false，无 backlog，末尾 commit | 1 | 连发 32 次 write（每次 cqe=1, commit=false），第 32 次改 commit=true。sqHead 0→32 正好写满 32 BB。第 23 次时 outstanding=22 触发守卫，但 cqCommitted=0=cqTail → else 只打日志、继续写 | 第 32 个恰好填满不溢出；**再发第 33 个即覆盖第 1 个** | ⚠️ 零裕度，off-by-one 即覆盖 |
| 2 | 同 #1 | 2/混合 | 连发 WriteWithNotify（bbcnt=2, cqe=1, commit=false）。每次 +2 BB，发到第 16 次时 sqHead=32 已填满环。32 次共需 64 BB = 2×容量 | **第 17 次**（sqHead 32→34）覆盖第 1、2 个 BB | ❌ 必覆盖 |
| 3 | 单批次数=4D，**存在已提交 backlog** | 1 | 先发 8 个 write（cqe=1）并 Commit 但未被 poll → backlog=8 占 8 BB（cqTail=0）。再连发 32 次 commit=false。守卫第 14 次起触发，能 poll 掉 8 个 backlog，但本批 32 个仍要占满环 | 第 24 次 outstanding=8+24=32 填满；**第 25 次覆盖最旧 backlog #1** | ❌ backlog+批 > 32 覆盖 |
| 4 | 同 #3，但 backlog 是 cqe=0 批 | 任意 | backlog=8 个 cqe=0 的 write（cqCommitted 不含它们）。守卫触发时无法 poll 这 8 个 → 永久占 8 BB → 可用仅 24 | 本批第 24 次（bbcnt=1）撞满 → **第 25 次覆盖** | ❌ backlog 不可回收（BUG-E 同源） |
| 5 | 批量合约（末尾 1 cqe），批长=4D 次 | 1 | 连发 32 次：前 31 次 cqe=0/commit=false，第 32 次 cqe=1/commit=true。整批 32 BB 正好填满；回收全靠第 32 个 CQE（entryIdx=32），发第 33 个前才有 CQE 可 poll | 第 32 个填满、零裕度；**第 33 个前若已放行到 32，再发即覆盖** | ⚠️ 零裕度 |
| 6 | 批量合约，批长=4D 次 | 2/混合 | 前 31 次 bbcnt=2/commit=false + 末尾 cqe。第 16 次 sqHead=32 满，撑不到末尾 commit | **第 17 次覆盖** | ❌ 超容量 |
| 7 | 多个短批（每批 ≤D=8 次）串成总量 >4D，每批带 cqe 并 commit | 任意 | 每 8 个一批、末尾 commit 带 cqe：批1(1-8)commit→cqCommitted=8；批2(9-16)→16…。发批3/批4 时 outstanding 逼近 22 触发守卫，poll 掉批1/批2 的 CQE（entryIdx=8/16）→ sqTail 前进腾空间 | 无——可无限续发 | ✅ 守卫自动回收（**安全用法**） |
| 8 | cqe 全关，总量按次数逼近 4D | 任意 | 连发 32 次 write（**cqe=0**, commit 与否均可）。cqCommitted 恒 0，守卫无 CQE 可 poll 只打日志 | 第 32 个填满；**第 33 个覆盖第 1 个** | ❌ BUG-E，无 completion |

### 6.5 结论与安全改写

- **"次数 ≤ 4D"不安全**：bbcnt=2/混合翻倍超容量（#2/#6）；bbcnt=1 也仅"恰好不溢出、零裕度"，且与 backlog 叠加即溢出（#1/#3）。
- **唯一安全的 4D 口径**：约束改为 **未提交的 ΣwqeBbCnt < 4D − POLL_CQ_THRESHOLD，且保证发批前把已提交 backlog 回收到 `backlog + 本批BB < 4D`**。即按 **BB 数**算、留阈值、并计入 backlog。
- **更省心的等价做法（推荐）**：与其让用户按 4D 精算 BB 数，不如遵守原约束1（每批 ≤D 次）并在批间 commit（带 cqe）——如 #7，守卫自动回收，可支撑任意总量，且保留 2D 安全裕度。
- **根治（无论哪种口径）**：落地「残留增补」——守卫返回状态、`PostSend` 无法回收时阻塞/报错。这样即便用户按 4D 发、甚至发过头，也只会被挡住而非静默覆盖，用户无需精确知道剩余容量。
- **另注**：吃满容量会更频繁命中环尾 2-BB WQE 拆分（BUG-F），放宽前需先确认 HW owner 语义。
