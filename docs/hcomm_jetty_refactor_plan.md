# HcommJetty 重构对齐方案

## 1. 当前状态

`include/aicore/hcomm/hcomm_jetty.h` 已收编 DeepEP-Ascend-Nightly-main 的 handle.hpp，包含：

| 类型 | 命名空间 | 执行模型 | 功能 |
|------|----------|----------|------|
| `HcommJetty` | `AscendC::simt::jetty` | `__simt_callee__` | warp 协作 WQE + DWQE doorbell + commit |
| `HcommPeer` | `AscendC::simt::jetty` | `__simt_callee__` | GM Jetty 远端信息句柄 |
| `HcommSge` | `AscendC::simt::jetty` | - | SGE 描述符 |

---

## 2. 对齐方向

### 2.1 SIMT 路径

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
| `group` | N warps | 多个 warp 各自构建 1 个 WQE，共享 Jetty 内部批次状态 |
| `block` | 整个 block | block 内所有 warp 参与，统一 commit |

```c++
// 重构后的接口示意
template <int kNumSGEs, typename Group>
void Write(const Group& group, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge);
```

#### 原则三：DWQE 发布机制沿用 hcomm simt 实现

保留 hcomm simt 里 DWQE 的"写 128B doorbell window"原理：

1. WQE 先通过与 Hcomm SIMT 相同的 write-through store（`asc_stwt`）写到 SQ 槽
2. `Commit()` 将最终 WRITE 的 header 更新为强序、有 fence 且请求 CQE
3. `Commit()` 时：
   - cooperative 写入由每个参与 lane 执行 `asc_threadfence()`，再同步到 commit leader
   - commit leader 执行 `asc_threadfence()`，保证最终 header 写入完成
   - `asc_ldcg` 从 SQ 读取最终 WRITE 的前 128B
   - `asc_stwt` + `make_ulonglong2` 以 128-bit 粒度写入 `dwqe_addr`（`dbVa - 0x80`）
   - `asc_dcci_single` × 2 刷新 DWQE 两行 cache

```c++
// 保留的核心 doorbell 流程
asc_threadfence();
asc_stwt(&dwqe_words[i], make_ulonglong2(asc_ldcg(sq_words), asc_ldcg(sq_words + 1)));
asc_dcci_single(dwqe_addr);
asc_dcci_single(dwqe_addr + kNumWQEBBBytes);
```

#### 原则四：队列状态只保存在 GM

`Write()` 由每个 warp 的 lane 0 在 SQ context 的 GM packed head 上原子预留 WQE；`Commit()`
使用句柄内的普通标量批次起点定位最终 WQE，并统一发布 DWQE。普通用户不需要提供或初始化
`HcommPeerInfo / HcommJettyInfo / write_counter / num_writes` 等 UB workspace；DeepEP dispatch
可以由多个 warp 并行调用 `Write<12>()`，随后由 group 中一个线程统一调用 `Commit()`。
多 warp 场景必须在所有 lane 完成 `HcommJetty` 构造后做一次 group 同步，再允许任一 warp 调用
`Write()`，保证 group leader 记录的批次起点早于本批次的全部预留。

---

## 3. 重构不涉及的改动

以下已收编且正确的内容保持不变：

- `HcommPeer` 从 GM Jetty 解析远端 token/EID 的逻辑
- 直接地址模型（无对称内存偏移）
- 头文件收编位置（`hcomm_simt.h` / `hcomm_impl.h` 的 include 链）
