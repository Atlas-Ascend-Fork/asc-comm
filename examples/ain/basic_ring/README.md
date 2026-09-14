# AIN Basic Ring样例

## 概述

本样例展示如何在AscendC AIV Kernel中通过`ain/ain.h`接口使用AIN基础通信能力。样例采用多rank对称执行方式：每个rank向下一个rank执行`Put`，并对上一个rank执行`Get`，最后通过AinBarrierSession完成同步。

Host侧负责内存申请/初始化HCCL通信域/创建HCCL Team/注册对称window并创建AIV通信channel；Kernel侧只通过AIN接口提交数据面通信任务。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 产品 | `Ascend 950PR / Ascend 950DT` |
| NPU架构 | `dav-3510` |
| 通信协议 | `COMM_PROTOCOL_UB_CTP` |
| 通信引擎 | `COMM_ENGINE_AIV` |
| 部署形态 | 单机多卡 |
| CANN包 | 使用当前环境中`set_env.sh`指向的CANN包 |

> 单卡环境仅支持编译验证，实际运行需要至少2张NPU。

## 目录结构

```text
basic_ring
├── CMakeLists.txt          // 编译工程文件
├── README.md               // 中文说明
├── README_en.md            // 英文说明
├── basic_ring.asc          // Host侧资源准备、AICore侧AIN调用和共享定义
└── run.sh                  // 多rank启动脚本
```

## 样例流程

### Host侧资源准备

1. 调用`aclInit`、`aclrtSetDevice`、`aclrtCreateStream`初始化运行环境。
2. 通过`HcclGetRootInfo`和`HcclCommInitRootInfo`创建多rank HCCL通信域。
3. 配置`barrierCount`并调用`HcclWorldTeamCreate`创建world team。
4. 分别将`sendBuf`和`recvBuf`通过`HcclTeamWindowRegister`注册为对称window。
5. 调用`HcclTeamChannelsCreate`创建AIV + UB_CTP通信channel。
6. 下发`worldTeam`、`sendWin`、`recvWin`到Kernel。

### Kernel侧AIN通信

Kernel侧包含如下通信步骤：

1. 构造`AscendC::Ain`对象并初始化Hcomm UB临时工作区。
2. 当前rank通过`Put`将本rank标识写入下一个rank的`recvWindow[rankId]`。
3. 当前rank通过`Get`从上一个rank的`sendWindow[0]`读取数据到本rank的`recvWindow[rankNum + rankId]`。
4. 调用`Flush`等待AIN通信任务完成。
5. 通过`AinBarrierSession::Sync`完成rank间同步。

## 编译

在本样例目录下执行：

```bash
# cann包为默认路径安装时，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# cann包为指定路径安装时
# source ${install_path}/cann/set_env.sh

rm -rf build
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## 运行

编译完成后，在样例目录执行：

```bash
./run.sh <nranks>
```

例如两卡运行：

```bash
./run.sh 2
```

`run.sh`会为每个rank启动一个`basic_ring_demo`进程，并使用临时文件交换HCCL root info。脚本默认超时时间为300秒。

## 执行结果

每个rank会回读`recvBuf`并打印`Put`和`Get`结果，例如：

```text
PUT RES rank 0 recvBuf[1]=1 expect=1
GET RES rank 0 recvBuf[2]=1 expect=1
rank 0 test pass
```

所有rank均输出`test pass`表示样例执行成功。

## 编译选项说明

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | 当前样例仅支持`dav-3510`，`dav-3510`对应Ascend 950PR / Ascend 950DT。 |

## 注意事项

- 编译和运行前必须先加载CANN环境变量，确保`ASCEND_CANN_PACKAGE_PATH`、ASC CMake模块和运行时动态库可用。
- 样例依赖CANN中的`hccl`、`hcomm`、`ascendcl`和`runtime`库。
- 运行时rank数量不能超过当前可用NPU数量。
- 样例仅支持单机多卡运行，不支持跨节点多机运行。
