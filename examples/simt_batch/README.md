# Hcomm SIMT Batch 接口用例

[English](./README_en.md)

该用例沿用仓库中其他 SIMT 用例的双 rank 结构：rank 0 通过 `HcclGetRootInfo` 生成 root info，两个 rank
通过 `HcclCommInitRootInfo` 创建 HCCL 通信域，不依赖 rank table。Host 侧注册本地内存并获取 UBC CTP
通道；rank 0 启动单 lane SIMT Kernel，rank 1 校验远端结果。

## 最小可执行用例

`basic_op_kernel.cpp`提供用户首次使用batch接口时需要的完整最小路径：准备可容纳两项的UB，创建
BatchHandle，连续加入两个`WriteNbi`，调用一次`BatchCommit`发布，再由`Drain`等待完成。该路径没有
模板分发、混合接口或状态矩阵，可以直接编译并在双rank环境执行：

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh
```

Kernel侧的核心调用顺序为：

```cpp
constexpr uint32_t kItemCount = 2U;
constexpr uint32_t kWorkspaceBytes = AscendC::simt::HcommSimtBatchBufferBytes(kItemCount);
alignas(128) __ubuf__ uint64_t batchBuffer[kWorkspaceBytes / sizeof(uint64_t)];

AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0) != 0) {
    return;
}
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), kWorkspaceBytes, remote);
if (!batch.valid ||
    hcomm.WriteNbi(batch, remote, local, 64U) != 0 ||
    hcomm.WriteNbi(batch, remote + 64U, local + 64U, 64U) != 0 ||
    hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.Drain(batch) != 0) {
    return;
}
```

UB由调用方提供，容量也由调用方选择；`HcommSimtBatchBufferBytes`公开当前基础格式的空间计算规则，
避免用户依赖内部WQE常量。BatchHandle绑定`remote`所在的一个远端注册区，后续数据地址和Notify地址
必须保持在该注册区内，库会基于handle缓存的base/size做轻量范围检查。

## 完整回归设计

功能用例以显式 batch 的常用调用形态为主，而不是用一个混合 batch 代替所有接口的验证：

| 场景 | batch 形态 | 主要验证点 |
| --- | --- | --- |
| Write/Read/Notify/WriteValue/FAA/CAS 同类 batch | 每类 2 批，每批 8 项，两次 `BatchCommit` 后统一 `Drain` | 每个接口独立作为发布尾项、连续提交和完成 |
| RMA 数据 | 每项 64B，16 项使用不同数据 | 长度、地址和 WQE 不串项 |
| FAA | 8 个目标各累加 2 次，16 个 fetch 独立保存 | 不丢失、不重复，并校验每次旧值 |
| CAS | 8 个目标各 CAS 2 次，16 个 fetch 独立保存 | 首次成功、第二次比较失败及每次旧值 |
| 混合 partial batch | 六类接口各一项，共 6 项 | 不同 WQE 类型可以共存，CAS 作为尾项 |
| 单项 batch | 一个 `WriteValueNbi` | `batch-size=1` 边界 |

用例首先记录handle的有效性、是否绑定到`RemoteBufferAddr`选择的注册区、逻辑容量和实际SQ深度；
每个场景分别记录`Append`、`BatchCommit`、
`Drain` 和 `BatchCommitCount`，并检查 rank 0 的
Read/FAA/CAS 返回值以及 rank 1 的全部远端数据。任一设备返回码、发布次数或数据不匹配都会使对应
rank 返回非零，不能仅凭 Kernel 返回成功打印 PASS。

普通 immediate 接口的单接口正确性由仓库现有 SIMT 用例负责；性能用例使用相同参数逐接口比较
Immediate 与 ExplicitBatch。这里保留混合与单项场景作为补充，但同类多项、多批提交是主要通过门槛。

当前基础版每项使用128B UB空间，优先复用已经验证的immediate WQE布局。用例通过公开的
`HcommSimtBatchBufferBytes`计算workspace，不直接依赖内部魔法数字。按接口压缩WQEBB
属于后续性能优化，不是当前接口语义的一部分。

## 通过标准

- handle有效并绑定到目标远端注册区，八个功能场景的设备步骤全部返回0，BatchCommit调用次数符合设计；
- rank 0 的 Read 数据、FAA 每次旧值、CAS 每次旧值全部正确；
- rank 1 的 Write/Notify/WriteValue 数据、Notify 信号、FAA/CAS 最终值全部正确；
- 两个 rank 和最终 `SUITE` 均输出 PASS，否则脚本返回非零。

## 环境要求

- 支持 `dav-3510` 的 CANN/ASC 编译环境及 UBC CTP 双 rank 运行环境；
- 已安装包含本分支 SIMT batch API 与实现的 asc-comm 开发包。

```bash
source /usr/local/Ascend/cann/set_env.sh
```

## 一键构建与执行

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh  # 最小使用路径
bash run.sh
```

`run.sh`执行六接口完整回归，也可显式执行`bash run.sh full`。可通过
`BUILD_JOBS=8 bash build.sh`指定并行编译数。执行脚本自动创建并清理root-info/同步文件，
并首先打印当前 `CodeVersion`。关键日志格式如下：

```text
STATUS | Case=WriteNbi.homogeneous | Step=BatchCommit | Code=0 | PASS
CHECK | API=AtomicFAA.fetch | ...
[rank 0] simt_batch PASS
[rank 1] simt_batch PASS
SUITE | Name=simt_batch | CodeVersion=... | Status=PASS
```
