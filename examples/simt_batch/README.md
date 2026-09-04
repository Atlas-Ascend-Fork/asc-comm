# Hcomm SIMT Batch 接口用例

[English](./README_en.md)

该用例支持2卡或8卡：rank 0通过`HcclGetRootInfo`生成root info，所有rank通过
`HcclCommInitRootInfo`创建同一个HCCL通信域，不依赖rank table。相邻rank组成独立通信对：偶数rank
0/2/4/6启动SIMT Kernel，奇数rank 1/3/5/7校验对应远端结果。

## 最小可执行用例

`basic_op_kernel.cpp`提供用户首次使用batch接口时需要的完整最小路径：准备共享上下文UB，创建
BatchHandle，依次执行两组“一个`WriteNbi`加一个`BatchCommit`”，再由`Drain`等待完成。该路径没有
模板分发、混合接口或状态矩阵，可以直接编译并在2卡或8卡环境执行：

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh
```

Kernel侧的核心调用顺序为：

```cpp
constexpr uint32_t kWorkspaceBytes = AscendC::simt::HcommSimtBatchBufferBytes(1U);
alignas(128) __ubuf__ uint64_t batchBuffer[kWorkspaceBytes / sizeof(uint64_t)];

AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0) != 0) {
    return;
}
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), kWorkspaceBytes, remote);
if (batch.context == nullptr ||
    hcomm.WriteNbi(batch, remote, local, 64U) != 0 ||
    hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.WriteNbi(batch, remote + 64U, local + 64U, 64U) != 0 ||
    hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.Drain(batch) != 0) {
    return;
}
```

UB由调用方提供，用来保存一份队列地址和远端Jetty静态信息，大小不再随Lane数增长。
BatchHandle绑定`remote`所在的远端注册区，后续数据地址和Notify地址必须保持在该注册区内；这是调用方
契约，热路径不再重复做范围检查。单Lane每批准备一项；Group Batch中每个Lane准备一项，因此
batch size等于Lane数，并且同一批操作必须具有相同WQEBB数。

## 完整回归设计

功能用例以显式 batch 的常用调用形态为主，而不是用一个混合 batch 代替所有接口的验证：

| 场景 | batch 形态 | 主要验证点 |
| --- | --- | --- |
| Write/Read/Notify/WriteValue/FAA/CAS 单Lane | 每项一次 `Append` 和一次 `BatchCommit`，最后统一 `Drain` | 每个接口的单项边界、连续提交和完成 |
| Write/Read/Notify/WriteValue/FAA/CAS Group Batch | 每批N个Lane，每Lane一项 | 同BB操作的多Lane并行SQ写入和一次发布 |
| RMA 数据 | 每项 64B，16 项使用不同数据 | 长度、地址和 WQE 不串项 |
| FAA | 8 个目标各累加 2 次，16 个 fetch 独立保存 | 不丢失、不重复，并校验每次旧值 |
| CAS | 8 个目标各 CAS 2 次，16 个 fetch 独立保存 | 首次成功、第二次比较失败及每次旧值 |
| APIs sequential | 六类接口各提交一个单项batch | 同一handle可连续复用到不同接口 |

用例首先记录handle上下文和实际SQ深度；
每个场景分别记录`Append`、`BatchCommit`、
`Drain` 和 `BatchCommitCount`，并检查每对中偶数rank的
Read/FAA/CAS返回值以及奇数rank的全部远端数据。任一设备返回码、发布次数或数据不匹配都会使对应
rank 返回非零，不能仅凭 Kernel 返回成功打印 PASS。

普通 immediate 接口的单接口正确性由仓库现有 SIMT 用例负责；性能用例使用相同参数逐接口比较
Immediate、单Lane单项Batch和Group Batch。Group路径是主要的批量性能路径。

Append只在Lane私有状态中生成位置无关的请求字段；`BatchCommit`由Lane 0一次预留整批SQ范围，
各Lane把请求与共享Jetty信息直接合并写入最终SQ槽位，全部写完后由尾Lane写DWQE发布。

## 通过标准

- handle上下文有效，全部设备步骤返回0，BatchCommit调用次数符合设计；
- 每对偶数rank的Read数据、FAA每次旧值、CAS每次旧值全部正确；
- 每对奇数rank的Write/Notify/WriteValue数据、Notify信号、FAA/CAS最终值全部正确；
- 2个或8个rank和最终`SUITE`均输出PASS，否则脚本返回非零。

## 环境要求

- 支持`dav-3510`的CANN/ASC编译环境及2卡或8卡UBC CTP运行环境；
- 已安装包含本分支 SIMT batch API 与实现的 asc-comm 开发包。

```bash
source /usr/local/Ascend/cann/set_env.sh
```

## 一键构建与执行

```bash
cd examples/simt_batch
bash build.sh
bash run_basic.sh                 # 默认2卡最小使用路径
bash run_basic.sh --devices 8     # 8卡最小使用路径
bash run.sh --devices 2           # 默认完整回归
bash run.sh matrix --devices 8    # 8卡完整矩阵
```

`--devices`只接受2或8，默认值为2。8卡模式并发执行0→1、2→3、4→5和6→7四对验证。
`run.sh`执行六接口完整回归，也可显式执行`bash run.sh full`。可通过
`BUILD_JOBS=8 bash build.sh`指定并行编译数。执行脚本自动创建并清理root-info/同步文件，
并首先打印当前 `CodeVersion`。关键日志格式如下：

```text
STATUS | Case=WriteNbi.homogeneous | Step=BatchCommit | Code=0 | PASS
CHECK | API=AtomicFAA.fetch | ...
[rank 0] simt_batch PASS
[rank 1] simt_batch PASS
SUITE | Name=simt_batch | CodeVersion=... | Devices=8 | Pairs=4 | Status=PASS
```
