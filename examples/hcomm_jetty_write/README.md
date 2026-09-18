# Hcomm Jetty WRITE样例

## 概述

本样例展示如何在AscendC AIV Kernel中通过`jetty/hcomm_jetty.h`接口使用URMA Jetty点对点通信能力。样例采用多rank对称执行方式：每个rank向其余所有rank执行一次`HcommJetty::Write`和一次`HcommJetty::WriteValue`，最后通过`HcommJetty::Drain`等待完成。

Host侧负责初始化HCCL通信域、注册通信内存、创建UBC_CTP channel并构造device侧Jetty table；Kernel侧通过`HcommPeer`和`HcommJetty`接口直接提交数据面通信任务，数据面执行期间不需要Host逐次参与。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 产品 | `Ascend 950PR / Ascend 950DT` |
| NPU架构 | `dav-3510` |
| 通信协议 | `COMM_PROTOCOL_UBC_CTP` |
| 通信引擎 | `COMM_ENGINE_AIV` |
| 部署形态 | 单机多卡 |
| CANN包 | 使用当前环境中`set_env.sh`指向的CANN包 |

> 单卡环境仅支持编译验证，实际运行需要至少2张NPU。

## 目录结构

```text
hcomm_jetty_write
├── CMakeLists.txt          // 编译工程文件
├── README.md               // 中文说明
├── README_en.md            // 英文说明
├── hcomm_jetty_write.asc   // Host侧资源准备、AICore侧Jetty调用和共享定义
└── run.sh                  // 多rank启动脚本
```

## 样例流程

### Host侧资源准备

1. 调用`aclInit`、`aclrtSetDevice`、`aclrtCreateStream`初始化运行环境。
2. 通过`HcclGetRootInfo`和`HcclCommInitRootInfo`创建多rank HCCL通信域。
3. 通过`HcclRankGraphGetLayers`、`HcclRankGraphGetLinks`和`HcclChannelDescInit`为每个非本rank peer构造`COMM_ENGINE_AIV` + `COMM_PROTOCOL_UBC_CTP`的channel描述。
4. 注册通信buffer并通过`HcclChannelGetRemoteMems`获取远端内存信息，用于计算外部提供的远端目标device地址。
5. 调用`HcclCreateChannels`创建channel，将channel handle写入device侧Jetty table，同时下发本端buffer地址、远端目标地址和远端buffer index。
6. 下发一个kernel后，Host侧通过HCCL barrier同步并回读校验。

### Kernel侧Jetty通信

Kernel侧包含如下通信步骤：

1. 遍历所有peer context，用用户分配的UB临时空间构造`HcommPeer`和`HcommJetty`对象。
2. 调用`HcommJetty::Write`向远端接收槽位写入256字节数据。
3. 调用`HcommJetty::WriteValue`向独立marker地址写入标识值。
4. 调用`HcommJetty::Drain`等待全部WQE完成。

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

`run.sh`会为每个rank启动一个`hcomm_jetty_write`进程，并使用临时文件交换HCCL root info。脚本默认超时时间为300秒。

## 执行结果

每个rank会校验数据槽和marker，例如：

```text
rank 0/2: Jetty WRITE and WriteValue validation passed for 1 peers
```

所有rank均输出`Jetty WRITE and WriteValue validation passed`表示样例执行成功。

## 编译选项说明

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | 当前样例仅支持`dav-3510`，`dav-3510`对应Ascend 950PR / Ascend 950DT。 |

## 注意事项

- 编译和运行前必须先加载CANN环境变量，确保`ASCEND_CANN_PACKAGE_PATH`、ASC CMake模块和运行时动态库可用。
- 样例依赖CANN中的`hccl`、`hcomm`、`ascendcl`和`runtime`库。
- 运行时rank数量不能超过当前可用NPU数量。
- 样例仅支持单机多卡运行，不支持跨节点多机运行。
