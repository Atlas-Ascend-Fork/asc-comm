# CCU AllToAll 直接执行样例

## 样例介绍

本样例使用 HCCL 通信域资源、full-mesh CCU channel 和 `ccu::Write` 实现等长 AllToAll。
每个 rank 向通信域内每个目标 rank 发送相同数量的数据，同时从每个源 rank 接收相同数量的数据。

当前实现范围：

- 仅支持 `HCCL_DATA_TYPE_FP32`。
- 每个源到每个目标的元素数由 `perCount` 指定。
- 输入和输出使用不同的 Device buffer，不支持原地操作。
- 支持最多 16 个 rank 的单机 1D full mesh。
- 每个通信域执行一次，未实现 engine context 缓存复用。

## 与参考实现的关系

参考文件：

```text
/mnt/workspace/gitCode/cann/hccl_api_test/custom_op_ccu/op_kernel/all_to_all_custom_v3.cpp
```

参考实现属于 AscendC/MC2 图算子路径：

```cpp
Hccl<HcclServerType::HCCL_SERVER_TYPE_CCU> hccl;
hccl.InitV2(context, &tilingData);
hccl.SetCcTilingV2(...);
hccl.AlltoAll(sendBuf, recvBuf, count, dataType, strideCount, repeat);
```

它由 host tiling 计算 `tileNum`、`tailNum` 和 `strideCount = M * K / rankDim`，通过高层
`hccl.AlltoAll` 接口把通信提交给 CCU server。其 host 侧算法配置为：

```text
AlltoAll=level0:fullmesh;level1:pairwise
```

本样例保持相同的数据布局和 CCU 通路，但使用直接 CCU kernel：Host 显式申请 thread/channel、注册 kernel，
kernel 显式交换输出地址和 token，并使用 `ccu::Write` 完成每个 peer 的数据搬运。这里的 `perCount`
对应参考实现一次完整 AllToAll 中每个 peer 的 `strideCount`。

## 输入输出布局

设通信域大小为 `N`，每个 peer 的发送元素数为 `perCount`。每个 rank 的输入和输出元素总数相同：

```text
totalCount = perCount * rankSize
```

源 rank `r` 的输入按目标 rank 排列：

```text
rank r sendBuf = [to_rank_0 | to_rank_1 | ... | to_rank_N-1]
```

目标 rank `p` 的输出按源 rank排列：

```text
rank p recvBuf = [from_rank_0 | from_rank_1 | ... | from_rank_N-1]
```

因此源 rank `r` 到目标 rank `p` 的地址映射是：

```text
src = rank r sendBuf + p * perCount * sizeof(dataType)
dst = rank p recvBuf + r * perCount * sizeof(dataType)
```

AllToAll 不执行归约。每一对 `(source rank, destination rank)` 对应唯一的输出区间，因此使用普通
`ccu::Write`，不同源 rank 的写入不会发生地址重叠。

## 执行流程

1. `main` 初始化 ACL，取得设备数并生成 HCCL root info。
2. 每个设备启动一个 Host 线程，绑定 NPU，创建 communicator 和 stream。
3. `HcclAllToAllCustom` 校验参数并取得 `myRank/rankSize`。
4. `AllocAlgResource` 为每个 peer 申请 `COMM_PROTOCOL_UBC_CTP` channel，并注册
   `CcuAllToAllMesh1DMem2MemKernel`。
5. `ExecOp` 计算 `rankSliceSize = perCount * sizeof(float)`，输入和输出总大小均为
   `rankSliceSize * rankSize`。
6. Host 分别取得 input token 和 output token，并把地址、token、rank segment 跨度及 LoopGroup 参数传入 kernel。
7. kernel 通过 channel 向所有 peer 发布本 rank 的输出基地址和 output token。
8. rank `r` 把本地输入的 `to_rank_r` 段拷到本地输出的 `from_rank_r` 段。
9. rank `r` 遍历其它目标 `p`，把本地 `to_rank_p` 段写到 rank `p` 输出的 `from_rank_r` 段。
10. 等待本地拷贝和所有远端写完成，再执行 channel 后同步。
11. Host 等待 stream、拷回输出，并按源 rank/目标 rank 地址公式检查结果。

大数据按每个 peer segment 内的相同偏移分片。第 `loop` 次 launch 中，Host 同时移动输入和输出基地址，
kernel 仍使用完整的 `rankSliceSize` 在不同 rank segment 之间寻址，因此不会改变数据布局。

## 两卡示例

测试固定使用 `perCount = 2`，rank `d` 的输入元素为 `d * 100 + i`：

```text
rank 0 sendBuf = [0,   1 | 2,   3]
                    to 0    to 1

rank 1 sendBuf = [100, 101 | 102, 103]
                      to 0      to 1
```

通信过程：

```text
rank 0 -> rank 0: [0, 1]
rank 0 -> rank 1: [2, 3]
rank 1 -> rank 0: [100, 101]
rank 1 -> rank 1: [102, 103]
```

输出按源 rank 拼接：

```text
rank 0 recvBuf = [0, 1 | 100, 101]
rank 1 recvBuf = [2, 3 | 102, 103]
```

## 目录结构

```text
custom_ccu_all_to_all/
|-- CMakeLists.txt
|-- README.md
|-- run.sh
|-- main.cc
|-- op_host/
|   |-- CMakeLists.txt
|   |-- alg_resource.cc
|   |-- alg_resource.h
|   |-- exec_op.cc
|   `-- exec_op.h
`-- op_kernel_ccu/
    |-- CMakeLists.txt
    |-- ccu_kernel.cc
    `-- ccu_kernel.h
```

## 构建和运行

执行前需要完成 asc-comm 数据面库构建或安装，并设置 CANN 与 CCU 调度环境：

```bash
source "${ASCEND_HOME_PATH}/set_env.sh"
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```
