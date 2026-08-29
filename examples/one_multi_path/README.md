# HCCL One Multi Path样例

## 概述

本样例演示如何通过HCCL Resource API查询`COMM_PROTOCOL_UB_MEM`链路，在同一个通信域中为每个peer创建`pathMode=1`和`pathMode=2`两条Channel。各peer按顺序处理；处理单个peer时，通过两个Stream并发搬运
不同的数据分片。

样例采用一进程一Device的运行方式。每个rank使用自己的rank ID填充本端HCCL Buffer，再从其他rank读取
4 KB数据并校验。`run.sh`负责启动本机rank进程，rank 0进程负责分发`HcclRootInfo`。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
one_multi_path
├── CMakeLists.txt          // 编译工程文件
├── main.cpp                // Host侧通信域、Channel和校验流程
├── README.md               // 中文样例说明
├── README_en.md            // 英文样例说明
├── run.sh                  // 多rank启动脚本
└── ubmem_data_copy.asc     // Ascend C数据搬运Kernel
```

## 样例描述

### 功能说明

每个rank先获取本端HCCL Buffer，并使用自己的rank ID填充前4 KB。Channel创建完成后，每个rank通过每个
peer对应的两条Channel读取对端HCCL Buffer，将读取结果写入本端校验Buffer，最后拷贝到Host侧逐字节校验。

```text
对端HCCL Buffer -> UB_MEM链路 -> Ascend C DataCopy -> 本端校验Buffer -> Host校验
```

同一个peer对应的两条Channel使用相同的通信Endpoint，只通过`ubMemAttr.pathMode`区分路径：

| Channel顺序 | 链路协议 | `pathMode` | 路径类型 | 数据范围 |
| --- | --- | --- | --- | --- |
| 1 | `COMM_PROTOCOL_UB_MEM` | `1` | one path | 前2 KB |
| 2 | `COMM_PROTOCOL_UB_MEM` | `2` | multi path | 后2 KB |

### 实现流程

1. rank 0生成并分发一份`HcclRootInfo`，各rank初始化同一个通信域。
2. 每个rank获取本端HCCL Buffer并写入rank ID，再通过`HcclCommMemReg`注册Channel所需的内存资源。
3. 每个rank遍历RankGraph各层，为每个peer筛选一条`COMM_PROTOCOL_UB_MEM`链路。
4. 每个peer基于同一组Endpoint构造`pathMode=1`和`pathMode=2`两个描述。
5. 将所有peer的Channel描述一次性传入`HcclChannelAcquire`，批量创建Channel。
6. 各peer按顺序处理；单个peer的两条Channel分别在两个Stream上读取前后两个2 KB分片，两个Kernel均下发后
   再同步Stream，因此并发范围仅限同一peer的两条path。
7. 将本端校验Buffer拷贝到Host，检查每个字节是否等于对端rank ID。
8. 所有peer的数据校验完成后执行`HcclBarrier`，确保全部rank完成远端读取后再销毁通信资源。

`rank_graph_topo`日志只显示所选UB_MEM链路所在的RankGraph层，不参与`pathMode`选择。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的[安装方式](../../docs/quick_start.md)配置
  环境变量。

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 单机两卡运行

  默认启动两个进程，分别使用Device 0和Device 1。

  ```bash
  cmake -S . -B build -DCMAKE_ASC_ARCHITECTURES=dav-3510
  cmake --build build -j
  bash run.sh
  ```

- 启动参数说明

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `-pes` | `2` | 通信域中的总rank数，必须为不小于2的整数 |
  | `-ipport` | `tcp://127.0.0.1:8899` | rank 0分发`HcclRootInfo`使用的地址；IPv6地址使用`tcp://[地址]:端口`格式 |
  | `-gnpus` | `2` | 当前机器启动的rank进程数 |
  | `-fpe` | `0` | 当前机器的起始rank ID |
  | `-fnpu` | `0` | 当前机器的起始Device ID |

- 跨机运行

  两机各4卡、总共8卡时，先在rank 0所在机器执行第一条命令，再在另一台机器执行第二条命令：

  ```bash
  # 机器A：rank 0-3，Device 0-3
  bash run.sh -pes 8 -ipport tcp://<机器A_IP>:8899 -gnpus 4 -fpe 0 -fnpu 0

  # 机器B：rank 4-7，Device 0-3
  bash run.sh -pes 8 -ipport tcp://<机器A_IP>:8899 -gnpus 4 -fpe 4 -fnpu 0
  ```

  两机各8卡、总共16卡时，将`-pes`和`-gnpus`分别设置为`16`和`8`，两台机器的`-fpe`分别设置为
  `0`和`8`。

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  日志顺序可能因多进程并发而不同。每个rank都应完成到其他rank的数据校验，例如：

  ```text
  rank 0/2: device=0, channels_per_peer=2, channel_descs=2
  rank 0: peer=1, rank_graph_topo=5, protocol=UB_MEM, path_modes=1/2
  rank 0: dual-path data copy from remote rank 1 passed, bytes=4096
  rank 0: dual-path validation passed
  ```

  所有rank均为每个peer取得两条不同的Channel Handle、完成数据校验且脚本返回0，表示样例执行成功。

## 注意事项

- 默认场景需要Device 0和Device 1可用，且两个Device之间存在`COMM_PROTOCOL_UB_MEM`链路。
- 多机运行时，所有机器必须使用相同的`-pes`，rank范围必须完整且不能重复。
- `-ipport`只用于分发`HcclRootInfo`，不参与HCCL数据链路选择。
- `HcclChannelAcquire`失败时，可根据前序`peer`链路筛选日志定位失败范围。
- 出现`no UB_MEM link found`时，表示当前rank与目标rank之间不存在可用的
  `COMM_PROTOCOL_UB_MEM`链路。
