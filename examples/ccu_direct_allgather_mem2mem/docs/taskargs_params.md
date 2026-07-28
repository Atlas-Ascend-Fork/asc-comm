# taskArgs / LoadArgs 参数说明

本样例 host 通过 `HcommCcuKernelLaunch` 下发 **15 个 `uint64` 的 taskArgs**，
kernel 用 `ccu::LoadArg(var, argId++)` 按**同一顺序**逐个读入。**host 下发顺序与
kernel LoadArgs 顺序必须严格一致**，否则参数错位、直接搬错数据。

数据约定：FP32，`dataSize = count * 4`（单 rank 字节数）；AllGather 后每 rank 的
output 长度 = `count * rankSize`，rank r 的数据落在 output 的
`[r*dataSize, (r+1)*dataSize)` 段。

## 15 个参数逐项

| idx | host 变量 | kernel 字段 | 本样例取值 | 含义 |
|----|-----------|-------------|-----------|------|
| 0 | `baseInputAddr` | `input` | sendBuf 地址 | 本 rank 输入 buffer 首地址 |
| 1 | `baseOutputAddr` | `output[rankId]` | recvBuf 地址 | 本 rank 输出 buffer 首地址（自己那一份的 output 基址） |
| 2 | `token` | `token[rankId]` | HcommCcuGetMemToken 结果 | 本 rank 内存的访问 token（CCU 远端访问凭证） |
| 3 | `currentRankSliceInputOffset` | `currentRankSliceInputOffset` | `0` | 输入切片偏移：本 rank 从输入的哪个字节开始搬（单发恒 0） |
| 4 | `currentRankSliceOutputOffset` | `currentRankSliceOutputOffset` | `dataSize * myRank` | 输出切片偏移：本 rank 的数据要落在 output 的第几段（= 自己 rank 号 × 单 rank 字节数） |
| 5 | `tmpRepeatNum` | `tmpRepeatNum` | `UINT64_MAX - 1` | repeat 轮数的补码形式（`UINT64_MAX - repeatNum`）。**KFC 版不用**，预留 |
| 6 | `inputRepeatStride` | `inputRepeatStride` | `0` | 多轮时每轮输入地址步长。**KFC 版不用**，预留 |
| 7 | `outputRepeatStride` | `outputRepeatStride` | `0` | 多轮时每轮输出地址步长。**KFC 版不用**，预留 |
| 8 | `normalSliceSize` | `normalSliceSize` | `dataSize`（= count×4） | 本轮要搬的字节数（单发 = 整个 count 的字节数）。kernel 用它做 `ccu::Write` 的长度、并 `CCU_IF(normalSliceSize != 0)` 判空 |
| 9 | `lastSliceSize` | `lastSliceSize` | `0` | 尾块字节数。**KFC 版不用**（尾块由 goSize 的 m/n/p 切分覆盖），预留 |
| 10 | `isInputOutputEqual` | `isInputOutputEqual` | `in==out ? 1 : 0`（本样例 in≠out → 0） | 是否 in-place（输入输出同一 buffer）。**KFC 版不用**，预留 |
| 11 | `goSize[0]` | `goSize.addrOffset` | CalGoSize 结果 | GroupCopy 分片参数：整块段的总偏移（m 段） |
| 12 | `goSize[1]` | `goSize.loopParam` | CalGoSize 结果 | GroupCopy 分片参数：loop 迭代打包字（含 iterNum） |
| 13 | `goSize[2]` | `goSize.parallelParam` | CalGoSize 结果 | GroupCopy 分片参数：并发打包字（含 n 段 repeat） |
| 14 | `goSize[3]` | `goSize.residual` | CalGoSize 结果 | GroupCopy 分片参数：尾块（p 段）大小 |

## 分组理解

- **数据地址三件套（idx 0/1/2）**：input / output / token —— 搬运的源、目的、访问凭证。
- **切片偏移（idx 3/4）**：本 rank 在整体 input/output 里的起始位置。
  AllGather 里 `currentRankSliceOutputOffset = dataSize * myRank` 决定了本 rank
  的数据写到 output 的哪一段（Mesh1D 按 rank 顺序拼接）。
- **repeat / 原地 / 尾块（idx 5/6/7/9/10）**：**5 个 KFC 版不使用的预留字段**。
  KFC 参照 kernel 是单发、in≠out、单 mission，用不到这些。保留是为了：
  (a) host/kernel 参数表对齐、(b) 将来若接 repeat/原地能力不必改 host 接口。
  当前它们被 LoadArgs 读入 ctx 但不参与任何搬运逻辑。
- **normalSliceSize（idx 8）**：本次 `ccu::Write` 搬多少字节。也是 kernel 里
  `CCU_IF(ctx.normalSliceSize != 0)` 的判空条件（count=0 时跳过整个搬运）。
- **goSize 四件套（idx 11-14）**：host 端 `CalGoSize(normalSliceSize, config)`
  算出的 GroupCopy 分片参数。`CalGoSize` 把 size 拆成
  m 个整块循环 + n 个 memSlice + p 尾块，打包成这 4 个字。**尾块正是靠 goSize[3]
  覆盖，所以单发版不需要单独的 lastSliceSize**。
  config 固定 `loopCount=8`、`memSlice=4096*8`，host 与 kernel 侧 GroupCopy 必须一致。

## goSize 的位域（CalGoSize 内部）

`goSize` 4 个字由 host 的 `GetLoopParam` / `GetParallelParam` / `GetOffsetParam`
按固定位宽打包（与 asc-devkit CalGoSize、ccu_direct 逐位一致）：
- `GetMaxLoopIterNum = SetBits(12)`（loop 迭代号 12 位）；
- 切分：`m = size / (loopCount*memSlice)`，`n = 余 / memSlice`，`p = 再余`；
- 据 (n,p) 组合选 loopExtendNum / tailSize（见 `main.asc` 的 `CalGoSize`）。

## 与 KFC 参照签名的对应

pr_4261 KFC 参照 kernel 签名是 **9 个 `ccu::Variable`**：
`inputAddr, outputAddr, tokenInfo, outputOffset, sliceSize, goSize0..3`
（+ channels/rankSize/rankId 通过 kernelArg 传）。

本样例的 15 参数 = KFC 的 9 个有效参数
（idx 0,1,2,4,8,11,12,13,14 —— 注意 KFC 的 outputOffset 对应本样例的
currentRankSliceOutputOffset）**加上** 5 个预留字段（idx 3 的 inputOffset 恒 0、
5/6/7/9/10 的 repeat/尾块/原地）与之对齐但不使用。功能上等价于 KFC 的 9 参数版。
