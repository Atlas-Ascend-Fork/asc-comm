# 22 · API · 支持的 Flags

> 原文：NCCL API → NCCL API Supported Flags

## Window Registration Flags

用于 `ncclCommWindowRegister()`。

| Flag | 行为 |
| --- | --- |
| `NCCL_WIN_DEFAULT` | 默认行为。允许用户在调 NCCL 集合操作时传入**相对 buffer 头地址的任意偏移**。代价是由于 buffer 使用不对称，NCCL 性能可能不是最优 |
| `NCCL_WIN_COLL_SYMMETRIC` | 要求**所有 rank 相对 buffer 头地址的偏移必须相等**（由用户自行保证）。这让 NCCL 能以对称方式操作 buffer，提供**最佳性能** |
| `NCCL_WIN_STRICT_ORDERING` | 注册的同时，为使用 IB Verbs transport 的 window 操作保证**严格定序**。主要面向用于 GIN VA Signals 的 buffer（见 Signals and Counters） |

选择建议：能保证各 rank 偏移一致就用 `NCCL_WIN_COLL_SYMMETRIC`，这也是 device API 和 zero-CTA 优化所要求的（见 `13`、`14`）。

## Communicator CTA Policy Flags

设置在 `ncclConfig_t` 的 `CTAPolicy` 字段上。

| Flag | 行为 | 适用场景 |
| --- | --- | --- |
| `NCCL_CTA_POLICY_DEFAULT` | NCCL 自动调整资源使用，追求**最大性能** | 大多数应用 |
| `NCCL_CTA_POLICY_EFFICIENCY` | 优化 CTA 使用，尽可能用**最少的 CTA** 达到不错的性能 | 需要更好的**计算通信 overlap** 的应用 |
| `NCCL_CTA_POLICY_ZERO` | 尽可能**零 CTA**，即使这个选择可能牺牲一些性能 | 应用必须为计算 kernel 保留最多 CTA 时 |

三者是"性能优先 → 兼顾 overlap → 完全让位于计算"的递进关系。

## Communicator Shrink Flags

修改 `ncclCommShrink` 操作的行为（用法见 `04-creating-a-communicator.md`）。

| Flag | 值 | 行为 |
| --- | --- | --- |
| `NCCL_SHRINK_DEFAULT` | `0x00` | 默认行为。收缩父 communicator，**不影响进行中的操作** |
| `NCCL_SHRINK_ABORT` | `0x01` | **先终止**父 communicator 上进行中的操作，**再**执行收缩。用于父 communicator 可能已 hang 的错误恢复场景 |

关于 `NCCL_SHRINK_ABORT` 的一个易漏点：**父 communicator 的资源仍然没有被释放**。shrink 之后是否对父 communicator 调 `ncclCommAbort`，由用户自己决定。
