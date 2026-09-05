# HcommJetty 重构对齐方案

## 1. 当前状态

`include/aicore/hcomm/hcomm_jetty.h` 已收编 DeepEP-Ascend-Nightly-main 的 handle.hpp，包含：

| 类型 | 命名空间 | 执行模型 | 功能 |
|------|----------|----------|------|
| `HcommPeer` | `AscendC` | `__aicore__` | 缓存远端 token/EID 到 ubuf |
| `HcommJetty` | `AscendC` | `__aicore__` | 标量 WQE 构建 + ubuf stage + SQ copy + drain |
| `HcommJetty` | `AscendC::simt::jetty` | `__simt_callee__` | warp 协作 WQE + DWQE doorbell + commit |
| `HcommSge` | `AscendC::simt::jetty` | — | SGE 描述符 |

---

## 2. 对齐方向

### 2.1 SIMD / AIV 路径 —— 保持现状

`AscendC::HcommJetty` 只做 Jetty 级能力，不改接口：

- 直接地址模型（dst/src 绝对 GM 指针）
- ubuf stage + `CopySqeToSq` + `WriteGmByPassDCache` 触发 doorbell
- `Put` / `PutWithNotify` / `Drain` 接口不变

### 2.2 SIMT 路径 —— 重构方向

#### 原则一：运行时 num_sges 改为模板参数

当前 `Write()` 接受运行时 `num_sges` 参数，每 lane 只贡献 1 个 SGE。

重构后改为固定 `kNumSGEs` 模板：

```c++
// 当前
void Write(const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge, const int& num_sges);

// 重构后
template <int kNumSGEs>
void Write(const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge);
```

好处：
- 编译期确定 WQE 布局（header lanes / SGE lanes），减少分支
- 与 SQE 的 `sgeNum` 硬件字段直接对应，无需运行时 mask

#### 原则二：引入 cooperative group 语义

当前接口隐含一个 warp = 一个 WQE 的假设，没有显式的 group 抽象。

重构后引入 cooperative group 分组，支持以下层级：

| Group 类型 | 粒度 | 语义 |
|-----------|------|------|
| `thread` | 单 lane | 单 SGE，不参与 shuffle |
| `warp` | 1 warp (32 lanes) | 一个 warp 协作构建 1 个 4BB WQE |
| `wrap` | N warps | 多个 warp 各自构建 1 个 WQE，共享 write_counter |
| `block` | 整个 block | block 内所有 warp 参与，统一 commit |

```c++
// 重构后的接口示意
template <int kNumSGEs, typename Group>
void Write(const Group& group, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge);
```

#### 原则三：DWQE 发布机制沿用 hcomm simt 实现

保留 hcomm simt 里 DWQE 的"写 128B doorbell window"原理：

1. WQE 先写到 SQ 槽（`asc_stcg`）
2. 同时将每 lane 的 SQE word 暂存到 `dwqe_stage`
3. `Commit()` 时：
   - `asc_threadfence()` 保证 SQ 写入完成
   - `asc_stwt` + `make_ulonglong2` 将 stage 的最后 128B 以 128-bit 粒度写入 `dwqe_addr`（`dbVa - 0x80`）
   - `asc_dcci_single` × 2 刷新 DWQE 两行 cache

```c++
// 保留的核心 doorbell 流程
asc_threadfence();
asc_stwt(&dwqe_words[i], make_ulonglong2(dwqe_stage[...], dwqe_stage[...+1]));
asc_dcci_single(dwqe_addr);
asc_dcci_single(dwqe_addr + kNumWQEBBBytes);
```

---

## 3. 重构不涉及的改动

以下已收编且正确的内容保持不变：

- `HcommPeerInfo` / `HcommJettyInfo` 结构体定义
- `HcommPeer` 构造函数（缓存远端 token/EID）
- 直接地址模型（无对称内存偏移）
- 命名空间划分：`AscendC` (SIMD) / `AscendC::simt::jetty` (SIMT)
- 头文件收编位置（`hcomm_simt.h` / `hcomm_impl.h` 的 include 链）
