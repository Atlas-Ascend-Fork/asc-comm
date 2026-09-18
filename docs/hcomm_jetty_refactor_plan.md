# HcommJetty 重构对齐方案

## 1. 当前状态

SIMT Jetty 的实现统一位于 `src/aicore/jetty/jetty_simt_impl.h`，公开包装入口为
`include/aicore/hcomm/hcomm_jetty_simt.h`。历史路径
`include/aicore/hcomm/hcomm_jetty.h` 仅保留兼容 include，不再复制实现。SIMD Jetty
继续使用 `include/aicore/jetty/hcomm_jetty.h`，两种执行模型分别维护自己的公开入口：

| 类型 | 命名空间 | 执行模型 | 功能 |
|------|----------|----------|------|
| `HcommJetty` | `AscendC::simt::jetty` | `__simt_callee__` | warp 协作 WQE + DWQE doorbell + commit |
| `HcommPeer` | `AscendC::simt::jetty` | `__simt_callee__` | GM Jetty 远端信息句柄 |
| `HcommSge` | `AscendC::simt::jetty` | - | SGE 描述符 |

SIMT 公开头通过 `hcomm_jetty_simt.h` 直接复用这套实现；`hcomm_simt.h` 也显式引用该
文件，避免再次引入兼容头。未使用的 `src/aicore/hcomm/impl/*jetty*` 副本已删除，SIMD
实现只保留在 `src/aicore/jetty/impl`。

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
| `warp` | 1 warp (32 lanes) | 一个 warp 按模板 SGE 数协作构建 1 个固定 2/3/4BB 槽 |
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
2. 模板 SGE 数决定固定槽大小，实际有效 SGE 仍按 WRITE 自身大小布局；剩余 BB 是独立 NOP WQE
3. `Commit()` 时：
   - cooperative 写入由每个参与 lane 执行 `asc_threadfence()`，再同步到 commit leader
   - 无 NOP 时，最终 WRITE header 携带最终 PI/CQE/fence，DWQE 读取该 WRITE 的头 128B
   - 有 NOP 时，最后一个 NOP header 携带最终 PI/CQE/fence，DWQE 从该独立 NOP 开始
   - commit leader 刷新本批次的完整 SQ 后再发布 DWQE
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
