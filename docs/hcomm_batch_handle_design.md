# Hcomm BatchHandle/BatchHandleRef 多协议设计方案

## 1. 背景

当前需要同时承接两类 Batch 能力：

- PR 111 的单 Channel Batch：创建 Batch 时已经确定 Channel 和远端内存；
- PR 97 的单 Jetty 多 Channel Batch：创建 Batch 后，通过 `GetHandleRef` 选择不同 peer。

后续还需要支持 UBC CTP、RoCE 等不同通信协议。不同协议的队列上下文、远端寻址信息和 WQE
格式不同，因此 Batch 类型不能固定为一套 UBC CTP 结构，也不应让未实现的协议直接复用 UBC CTP
类型。

本方案只确定 Handle/Ref 的语义、协议扩展方式和公开接口关系。具体字段布局、WQE 组装过程以及
多核并发访问同一 Jetty 的实现不在本方案范围内。

## 2. 设计结论

1. 保留 `BatchHandle` 和 `BatchHandleRef` 两个语义类型，不合并。
2. 保留 `BatchHandleTraits<commProtocol>`，同时提供 `Type` 和 `RefType`。
3. `Type` 表示整批任务的状态，供 `BatchCommit` 和 `Drain` 使用。
4. `RefType` 表示同一批任务中选中的一个 Channel/peer，供 `WriteNbi`、`ReadNbi` 等操作使用。
5. 单 Channel 和多 Channel 使用同一协议对应的 `Type/RefType`，区别只在创建和选择方式。
6. PR 111 的 `BatchHandleTraits::Type` 保留；多 Channel 能力在同一个 trait 中增加 `RefType`。
7. 当前只实现 UBC CTP specialization。RoCE 实现完成时再增加自己的 Handle、Ref 和 trait
   specialization，不提前映射到 UBC CTP 类型。

## 3. 类型语义

### 3.1 BatchHandleType

`BatchHandleType` 表示整批任务共享的执行状态，至少承担以下职责：

- 关联调用者提供的 UB Batch buffer；
- 维护已组装 WQE/CQE 数量；
- 维护 Batch 使用的 SQ/CQ 状态；
- 作为 `BatchCommit` 和 `Drain` 的唯一输入；
- 为 `BatchHandleRefType` 提供共享状态的关联方式。

`BatchHandleType` 是 Batch 状态的所有者或入口。复制 Handle 不能产生一份独立的 Batch 状态。

### 3.2 BatchHandleRefType

`BatchHandleRefType` 是选中 Channel 后得到的轻量引用，至少包含两类信息：

- 对原 `BatchHandleType` 状态的引用；
- 当前 Channel/peer 对应的协议专用远端信息。

Ref 不拥有 SQ/CQ、Channel 或远端 MR，不参与资源释放。多个 Ref 可以引用同一个 BatchHandle，所有
Ref 组装的 WQE 最终由同一个 `BatchCommit` 提交。

### 3.3 为什么不合并

如果合并成一个类型，`MakeBatchHandle` 返回值只有 Batch 状态有效，而 `GetHandleRef` 返回值的 Batch
状态和远端信息都有效。同一个类型将出现两种有效状态，下面的误用也无法在编译期阻止：

```cpp
auto batch = hcomm.MakeBatchHandle(multiChannel, buffer, bufferLen);
hcomm.WriteNbi(batch, dst, src, len); // 尚未选择Channel
```

拆分后，接口约束明确：

```text
BatchHandleType     -> BatchCommit / Drain
BatchHandleRefType  -> WriteNbi / ReadNbi / WriteWithNotifyNbi
```

## 4. 协议 Traits

概念结构如下：

```cpp
template <CommProtocol commProtocol>
struct BatchHandleTraits;

struct UbcCtpBatchHandle;
struct UbcCtpBatchHandleRef;

template <>
struct BatchHandleTraits<COMM_PROTOCOL_UBC_CTP> {
    using Type = UbcCtpBatchHandle;
    using RefType = UbcCtpBatchHandleRef;
    static constexpr bool supported = true;
};
```

RoCE 支持完成后独立扩展：

```cpp
struct RoceBatchHandle;
struct RoceBatchHandleRef;

template <>
struct BatchHandleTraits<COMM_PROTOCOL_ROCE> {
    using Type = RoceBatchHandle;
    using RefType = RoceBatchHandleRef;
    static constexpr bool supported = true;
};
```

UBC CTP 与 RoCE 的具体 Handle/Ref 可以采用不同布局。例如 UBC CTP Ref 可以保存 TP、EID 和 UB
token 信息，RoCE Ref 可以保存 QP、rkey 等信息。公开 `Hcomm` 接口不依赖这些字段。

当前代码中下面这种占位不建议保留：

```cpp
BatchHandleTraits<COMM_PROTOCOL_UBC_CTP> -> BatchHandle / BatchHandleRef
BatchHandleTraits<COMM_PROTOCOL_ROCE>    -> BatchHandle / BatchHandleRef
```

它会错误表达 RoCE Batch 已经支持，并限制后续协议使用自己的结构。

对于尚未支持 Batch 的协议，应保证普通 `Hcomm<protocol>` 接口仍可实例化。可以由主模板映射到内部
Unsupported 类型，并由通用实现返回 `HCOMM_FAILED`；不能因为缺少 Batch specialization 破坏已有的
非 Batch 接口。

## 5. Hcomm 公开类型

`Hcomm` 类保留两个协议相关别名：

```cpp
template <CommProtocol commProtocol = COMM_PROTOCOL_UBC_CTP>
class Hcomm {
public:
    using BatchHandleType = typename BatchHandleTraits<commProtocol>::Type;
    using BatchHandleRefType = typename BatchHandleTraits<commProtocol>::RefType;

    // ...
};
```

业务代码推荐使用 `auto` 接收 Handle 和 Ref，避免依赖协议内部结构：

```cpp
auto batch = hcomm.MakeBatchHandle(...);
auto peer = hcomm.GetHandleRef(batch, channelIndex);
```

## 6. 单 Channel Batch 接口

PR 111 的单 Channel 创建方式保持：

```cpp
BatchHandleType MakeBatchHandle(
    ChannelHandle channel,
    GM_ADDR remoteBase,
    GM_ADDR localBase,
    const LocalTensor<T>& buffer,
    uint32_t bufferLen);
```

创建时已经确定唯一 Channel，因此可以保留直接接收 `BatchHandleType` 的便捷操作：

```cpp
int32_t WriteNbi(BatchHandleType& batch, GM_ADDR dst, GM_ADDR src, uint64_t len);
int32_t ReadNbi(BatchHandleType& batch, GM_ADDR dst, GM_ADDR src, uint64_t len);
int32_t WriteWithNotifyNbi(BatchHandleType& batch, ...);
```

语义上，这些接口等价于使用该 Batch 的默认 Channel Ref。默认 Ref 如何保存由协议实现决定，不进入
公开 ABI。

## 7. 多 Channel Batch 接口

多 Channel 创建方式使用 `MultiChannelHandle`：

```cpp
BatchHandleType MakeBatchHandle(
    MultiChannelHandle multiChannel,
    const LocalTensor<T>& buffer,
    uint32_t bufferLen);
```

通过 `GetHandleRef` 选择 Channel：

```cpp
BatchHandleRefType GetHandleRef(
    BatchHandleType& batch,
    uint32_t channelIndex);
```

多 Channel 操作只接收 Ref：

```cpp
int32_t WriteNbi(BatchHandleRefType& ref, GM_ADDR dst, GM_ADDR src, uint64_t len);
int32_t ReadNbi(BatchHandleRefType& ref, GM_ADDR dst, GM_ADDR src, uint64_t len);
int32_t WriteWithNotifyNbi(BatchHandleRefType& ref, ...);
```

所有 Ref 最终使用原 BatchHandle 提交和等待：

```cpp
int32_t BatchCommit(BatchHandleType& batch);

template <pipe_t pipe = PIPE_MTE3>
int32_t Drain(BatchHandleType& batch);
```

典型调用流程：

```cpp
auto batch = hcomm.MakeBatchHandle(multiChannel, buffer, bufferLen);
auto peer0 = hcomm.GetHandleRef(batch, 0U);
auto peer1 = hcomm.GetHandleRef(batch, 1U);

hcomm.WriteNbi(peer0, dst0, src0, len);
hcomm.WriteNbi(peer1, dst1, src1, len);
hcomm.BatchCommit(batch);
hcomm.Drain(batch);
```

## 8. Handle 与 Ref 的关系

```text
                 BatchHandleType
                 整批共享状态
                       |
            +----------+----------+
            |                     |
     GetHandleRef(0)        GetHandleRef(1)
            |                     |
   BatchHandleRefType      BatchHandleRefType
      peer 0信息              peer 1信息
            |                     |
        WriteNbi               WriteNbi
            +----------+----------+
                       |
                 BatchCommit
                       |
                     Drain
```

必须满足以下约束：

- Ref 的有效期不能超过 BatchHandle 及其 UB buffer；
- Ref 对应的 Host Channel、远端 MR 表和 `MultiChannelHandle` 在 Device 使用期间必须有效；
- `channelIndex` 与 Host 组装的 Channel 顺序一一对应；
- Ref 只能用于创建它的 Batch，不能跨 Batch 混用；
- `BatchCommit` 和 `Drain` 只接收根 Handle，不接收 Ref。

## 9. 与 PR 111 和 PR 97 的关系

### 9.1 PR 111

PR 111 已经提供：

```cpp
BatchHandleTraits<commProtocol>::Type
```

本方案保留该设计，并扩展为：

```cpp
BatchHandleTraits<commProtocol>::Type
BatchHandleTraits<commProtocol>::RefType
```

PR 111 的单 Channel `MakeBatchHandle`、Batch Write/Read/Notify、`BatchCommit` 和 `Drain` 继续使用
`Type`。多 Channel 能力不再定义另一套同名 trait。

### 9.2 PR 97

PR 97 中 `GetHandleRef` 返回轻量远端引用的设计由 `RefType` 承接：

```cpp
auto ref = hcomm.GetHandleRef(batch, channelIndex);
hcomm.WriteNbi(ref, ...);
```

AICore 侧的 `MultiChannelHandle` 是底层类型为 `uint64_t` 的强类型枚举，与 `ChannelHandle` 可用于
重载分发；Host 侧仍使用 `uint64_t` 句柄。Host 负责将离散 Channel 的共享队列信息和逐 Channel
远端信息组装为 Device 可索引的数据，跨 Host/Device 的参数结构仅传递其 `uint64_t` 表示。

## 10. 文件职责建议

```text
include/aicore/hcomm/hcomm.h
    Hcomm公开别名和Batch接口声明

include/aicore/hcomm/hcomm_common.h
    公共协议枚举和必要的公共声明

src/aicore/hcomm/common/hcomm_inner_def.h
    协议专用Handle/Ref、BatchHandleTraits specialization

src/aicore/hcomm/common/hcomm_base.h
    未支持协议的通用失败实现

src/aicore/hcomm/impl/hcomm_impl.h
    Hcomm公开接口到HcommImpl的转发

src/aicore/hcomm/impl/platform_v310/hcomm_aiv_urma*.h
    UBC CTP Handle/Ref和Batch行为实现

include/hcomm/host/hcomm_batch.h
    Host侧MultiChannelHandle组装和释放
```

协议专用远端结构不建议继续使用无协议前缀的 `HcommBatchRemoteInfo` 暴露。实现调整时可改为
`UbcCtpBatchRemoteInfo`，后续由 RoCE 定义自己的 Ref 内容。

## 11. 迁移步骤

### 阶段一：整理类型，不改变行为

1. 保留 `BatchHandleTraits::Type/RefType` 和 `Hcomm` 中的两个别名；
2. 将当前通用 `BatchHandle/BatchHandleRef` 明确为 UBC CTP 具体类型；
3. 删除当前 RoCE 到 UBC CTP Handle/Ref 的占位映射；
4. 保证未支持 Batch 的协议仍能实例化普通 Hcomm 接口；
5. 样例继续使用 `auto`，不依赖具体类型名。

### 阶段二：吸收 PR 111

1. 以 PR 111 的 UBC CTP `BatchHandleTraits::Type` 为基础；
2. 将多 Channel 需要的 `RefType` 合入同一 specialization；
3. 单 Channel 和多 Channel `MakeBatchHandle` 返回相同的 `BatchHandleType`；
4. 统一 Write/Read/Notify 的 WQE 组装路径；
5. 统一 `BatchCommit` 和 `Drain`，避免维护两套 UBC CTP Batch 状态机。

### 阶段三：增加新协议

1. 定义协议专用 Handle、Ref 和远端信息；
2. 增加 `BatchHandleTraits<protocol>` specialization；
3. 实现协议对应的 Make/GetRef/Write/Commit/Drain；
4. Host 多 Channel 元数据增加对应协议的构建路径；
5. 不修改 Hcomm 公开调用流程。

## 12. 验收标准

- 单 Channel UBC CTP Batch 调用方式与 PR 111 一致；
- 多 Channel UBC CTP 通过 `GetHandleRef` 选择 peer；
- 单 Channel 和多 Channel 共用一套 UBC CTP BatchCommit/Drain 实现；
- `BatchHandleType` 不能传给要求 `BatchHandleRefType` 的多 Channel Write 接口；
- `BatchHandleRefType` 不能传给 BatchCommit/Drain；
- 未实现 Batch 的协议不复用 UBC CTP Handle/Ref；
- 新增协议只需要增加类型、trait specialization 和协议实现，不修改业务调用流程；
- Host 创建阶段的元数据组装不进入 Device 性能热路径。

## 13. 待验证项

### 13.1 同一 Channel 下不同 MR 的远端信息

当前设计暂按以下边界处理：

- `tpID`、`remoteEID` 属于 Channel/传输路径级信息，同一 Channel 下的不同 MR 预期相同；
- `addr`、`size`、`tokenId`、`tokenValue` 属于 MR 级信息，不假设不同 MR 相同；
- 一个 Channel 可以只保存一份 Channel 级信息，但必须保留完整的远端 MR 表，或为每个选中的 MR
  单独缓存其 token。

曾有实现只提取第一个远端 MR 的 token，并假设同一 Channel 的其他 MR 使用相同 token。实际运行出现
`remote buffer[1] does not share one UB token`，因此“一个 Channel 的全部 MR 共用一个 token”不能作为
当前实现前提。

需要在真实环境中继续验证：

1. 同一 Channel 注册多个远端 MR 时，`tpID` 和 `remoteEID` 是否始终一致；
2. 构造至少两个 token 不同的远端 MR，确认同一个 Channel Ref 能分别访问两个 MR；
3. 目标地址不属于任何远端 MR 时，`HcommFindBufferIdx` 失败必须返回 `HCOMM_FAILED`，不能继续使用
   无效下标访问 MR 表；
4. 对比每次 `WriteNbi` 查找 MR 与在 `GetHandleRef(batch, channelIndex, dstBase)` 阶段选定 MR、缓存
   token 两种方案的正确性和性能；后者访问多个 MR 时，应为同一 Channel 创建多个 Ref，并共享同一个
   BatchHandle 状态。

验证完成前，推荐模型为：

```text
一个 Channel -> 一份 TP/EID + 一张远端 MR 表
一个具体 MR  -> 独立的 addr/size/tokenId/tokenValue
```

## 14. 暂不处理的问题

- 多个 AICore 并发提交同一个 Jetty 时的 SQ/CQ 原子预留；
- 一个 Batch 内不同协议混用；
- Handle/Ref 的最终字段数量和 UB 布局；
- ABI 版本升级和跨版本兼容；
- RoCE Batch 的具体 WQE/CQE 实现。
