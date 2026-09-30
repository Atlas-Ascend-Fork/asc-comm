# Hcomm RoCE 多 SGE 批量读写样例

本样例演示在 RoCE 通道上调用 `MakeBatchHandle` 创建 `UbcBatchHandle`，并用其多 SGE 版本
`WriteNbi`、`ReadNbi` 连续提交离散内存段。MPI 负责多进程管理、HCCL root info 交换和各阶段同步。

## 样例行为

- `WriteNbi` 调用 128 次。每次调用的 `srcDescs` 包含 8 个 64B 离散数据段，远端 `dst` 是一个
  512B 连续数据区。
- `ReadNbi` 调用 128 次。每次调用的 `dstDescs` 包含 8 个 64B 离散数据段，远端 `src` 是一个
  512B 连续数据区。
- 128 次 `WriteNbi` 全部提交后，执行一次 `BatchCommit` 和一次 `Drain`；128 次 `ReadNbi`
  同样在全部提交后统一提交和清排。
- rank 0 执行写请求，rank 1 校验落地数据；rank 1 再执行读请求并校验本端离散段内容。

## 编译运行

本样例与 07 样例相同，直接使用 CANN 环境中已安装的 asc-comm 头文件；该安装包需要包含
RoCE 批量多 SGE 接口。

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
mpirun -n 2 ./hcomm_roce_multi_sge
```

运行要求 rank 0 与 rank 1 之间存在 RoCE 链路；每个节点上的 MPI 本地 rank 数不能超过可用 NPU 数。
