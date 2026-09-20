# SIMT Jetty WRITE样例

## 概述

本样例展示如何在AscendC SIMT Kernel中通过`jetty/hcomm_jetty_simt.h`接口使用URMA Jetty点对点通信能力。样例覆盖`thread / warp / group`三种协作方式，以及多个warp并行生成WQE、group统一提交的dispatch模式和SIMT/SIMD混合发布的mixed模式。

Kernel侧通过`HcommPeer`和`HcommJetty`接口直接从GM Jetty表解析队列与远端信息并提交数据面通信任务，不需要静态或动态UB workspace。固定SGE模板参数范围为2到12；地址或长度为0的SGE不计入WRITE，有效SGE自动压紧，未使用的WQEBB使用NOP对齐。

延迟提交路径由`Write<..., false>`、`AdvanceSq`和`PublishSq`组成：`AdvanceSq`完成SQ头推进与终止WQE标记，`PublishSq`写入128B DWQE窗口发布队列，二者缺一不可。立即提交路径`Put`在单次调用内完成WQE构造、SQ写入与DWQE发布。同一个channel的并发`Write`必须属于同一个batch，并在所有参与warp完成后由group leader统一提交；所有lane构造`HcommJetty`后、任一warp开始`Write`前必须先做一次group同步。

`mixed`模式使用`__global__ __mix__`入口：SIMT VF构造并提交SQ内容，AIV主体随后使用共享的80B Jetty元数据调用SIMD `RingDoorbell()`发布队列，保留SIMT/SIMD共享SQ的场景。mixed是correctness场景，不参与`perf.sh`的设备计时模式。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 产品 | `Ascend 950PR / Ascend 950DT` |
| NPU架构 | `dav-3510` |
| 通信协议 | `COMM_PROTOCOL_UB_CTP` |
| 执行模型 | SIMT VF（mixed模式额外包含AIV主体） |
| 部署形态 | 单机多卡 |
| CANN包 | 使用当前环境中`set_env.sh`指向的CANN包 |

> 单卡环境仅支持编译验证，实际运行需要至少2张NPU。

## 目录结构

```text
simt_jetty
├── CMakeLists.txt          // 编译工程文件
├── README.md               // 中文说明
├── README_en.md            // 英文说明
├── build.sh                // 编译脚本
├── run.sh                  // 多rank正确性执行脚本
├── stress.sh               // 同channel重复压测脚本
├── perf.sh                 // 设备侧计时性能脚本
├── main.cpp                // Host侧资源准备与结果校验
├── op_kernel.cpp           // SIMT Kernel侧Jetty调用
└── simt_jetty_common.h     // 样例共享定义
```

## 模式说明

| 模式 | 说明 |
| --- | --- |
| `thread` | 单线程构造12个有效SGE的4BB WQE，按2-SGE分段独立发布，验证标量路径 |
| `warp` | warp协作构造WQE，验证固定槽位预留的立即与延迟提交 |
| `warp-pad` | 1个有效SGE加零长度SGE，验证`1BB WRITE + 1BB NOP`填充路径 |
| `group` | 多warp共享batch状态，group leader统一提交 |
| `dispatch` | 每个warp各预留一个WQE，block统一提交实际数量 |
| `mixed` | SIMT VF构造SQ内容，AIV侧SIMD `RingDoorbell()`发布 |

## 编译

在本样例目录下执行：

```bash
# cann包为默认路径安装时，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# cann包为指定路径安装时
# source ${install_path}/cann/set_env.sh

bash build.sh
```

没有硬件时，可在编译阶段检查SIMT VF的栈和寄存器占用：

```bash
CCE_RES_USAGE=1 bash build.sh
```

默认SGE数量为12，可通过`SIMT_JETTY_SGE_NUM`重编译调整（范围1到12）：

```bash
cmake -DSIMT_JETTY_SGE_NUM=6 .. && make -j
```

## 运行

编译完成后，在样例目录执行：

```bash
./run.sh <nranks> [all|thread|warp|warp-pad|group|dispatch|mixed] [options]
```

例如两卡执行全部模式：

```bash
./run.sh 2
```

也可以只执行一个模式，避免失败用例污染后续SQ/CQ状态：

```bash
./run.sh 2 warp
./run.sh 2 mixed
```

通用选项：`--iterations N`指定正确性校验轮数（默认1），`--warmup N`与`--timing`供性能模式使用。

## 正确性压测

`stress.sh`在同一个channel/SQ上重复执行，每一轮均等待接收端校验完成。接收端校验后会清零目标缓冲区，避免后一轮未实际下发时被前一轮结果掩盖。默认每个模式执行100次：

```bash
./stress.sh 2
./stress.sh 2 warp-pad 1000
```

## 性能测试

`perf.sh`使用设备侧`clock()`打点。性能循环单独调用`Write`组WQE，在`Write`返回后记录`write_cycles`，再调用提交并单独记录`commit_cycles`，最后在`Drain`返回后记录完整完成耗时`completion_cycles`，因此`average_write_us`不包含提交时间。默认warmup 100次、计时1000次，每个warmup/计时样本单独launch一个kernel；每轮都会`Drain`，确保SQ tail持续推进。host launch和同步时间不计入上述设备周期：

```bash
./perf.sh 2
./perf.sh 2 warp 10000 1000
```

结果中的周期按当前设备1 GHz时钟换算为微秒，与`simt_urma_perftest`的统计方式一致。

## 执行结果

正确性模式下，rank 1校验接收缓冲区各槽位，例如：

```text
[rank 1] simt_jetty mode 4 iteration 0 PASS
```

所有请求的模式均输出`PASS`表示样例执行成功。

## 注意事项

- 编译和运行前必须先加载CANN环境变量，确保`ASCEND_CANN_PACKAGE_PATH`、ASC CMake模块和运行时动态库可用。
- 样例依赖CANN中的`hccl`、`hcomm`、`ascendcl`和`runtime`库。
- `nranks`必须大于等于2；样例只实际使用rank 0和rank 1，其余rank会直接退出。
- 样例仅支持单机多卡运行，不支持跨节点多机运行。
