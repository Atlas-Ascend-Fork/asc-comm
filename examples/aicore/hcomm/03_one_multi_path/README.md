# HCCL One Multi Path样例

## 概述

本样例演示如何通过HCCL Resource API查询`COMM_PROTOCOL_UB_MEM`链路，在同一个通信域中为每个peer
创建`pathMode=1`和`pathMode=2`两条Channel。各peer按顺序处理；处理单个peer时，通过两个Stream
并发搬运不同的数据分片。

样例采用一进程一Device的运行方式。每个rank使用自己的rank ID填充本端HCCL Buffer，再从其他rank
读取4 KB数据并校验。可执行文件直接拉起本机全部rank进程；多机部署时每台机器各起一个进程，
rank 0进程负责分发`HcclRootInfo`。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
one_multi_path
├── CMakeLists.txt      // 编译工程文件
├── main.cpp            // Host侧通信域、Channel和校验流程
├── ubmem_data_copy.asc // Ascend C数据搬运Kernel
├── README.md           // 中文样例说明
└── README_en.md        // 英文样例说明
```

## 样例描述

### 功能说明

每个rank先获取本端HCCL Buffer，并使用自己的rank ID填充前4 KB。Channel创建完成后，每个rank
通过每个peer对应的两条Channel读取对端HCCL Buffer，将读取结果写入本端校验Buffer，最后拷贝到
Host侧逐字节校验。

```text
对端HCCL Buffer -> UB_MEM链路 -> Ascend C DataCopy -> 本端校验Buffer -> Host校验
```

同一个peer对应的两条Channel使用相同的通信Endpoint，只通过`ubMemAttr.pathMode`区分路径：

| Channel顺序 | 链路协议 | `pathMode` | 路径类型 | 数据范围 |
| --- | --- | --- | --- | --- |
| 1 | `COMM_PROTOCOL_UB_MEM` | `1` | one path | 前2 KB |
| 2 | `COMM_PROTOCOL_UB_MEM` | `2` | multi path | 后2 KB |

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | 每peer两条`COMM_PROTOCOL_UB_MEM`路径（one path/multi path）并发读取 |
| 调用方式 | Host侧Resource API创建Channel + Ascend C DataCopy Kernel |
| 数据规模 | 每peer读取4 KB（两条path各2 KB） |
| 部署形态 | 单机多卡/多机多卡 |

### 实现流程

1. 可执行文件直接拉起本机rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换`HcclRootInfo`，
   各rank初始化同一个通信域。
2. 每个rank获取本端HCCL Buffer并写入rank ID，再通过`HcclCommMemReg`注册Channel所需的内存资源。
3. 每个rank遍历RankGraph各层，为每个peer筛选一条`COMM_PROTOCOL_UB_MEM`链路。
4. 每个peer基于同一组Endpoint构造`pathMode=1`和`pathMode=2`两个描述。
5. 将所有peer的Channel描述一次性传入`HcclChannelAcquire`，批量创建Channel。
6. 各peer按顺序处理；单个peer的两条Channel分别在两个Stream上读取前后两个2 KB分片，两个Kernel
   均下发后再同步Stream，因此并发范围仅限同一peer的两条path。
7. 将本端校验Buffer拷贝到Host，检查每个字节是否等于对端rank ID。
8. 所有peer的数据校验完成后通过控制通道barrier同步，确保全部rank完成远端读取后再销毁通信资源。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的[安装方式](../../../../docs/quick_start.md)配置环境变量。

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 单机两卡运行

  可执行文件内部fork出全部rank进程，Device与rank一一对应：

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/one_multi_path tcp://127.0.0.1:29625 <nranks>
  ```

- 启动参数说明

  单机模式：

  ```bash
  ./build/one_multi_path <ip:port> <nranks>
  ```

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 通信域中的总rank数，必须为不小于2的整数，且不超过本机可用NPU数量 |

- 跨机运行

  多机部署时每台机器直接运行可执行文件，逐rank指定全局rank与Device：

  ```bash
  ./build/one_multi_path <ip:port> <rank_size> <rank> <device>
  ```

  两机各4卡、总共8卡时，先在rank 0所在机器执行第一条命令，再在另一台机器执行：

  ```bash
  # 机器A：rank 0-3，Device 0-3
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 0 0
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 1 1
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 2 2
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 3 3

  # 机器B：rank 4-7，Device 0-3
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 4 0
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 5 1
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 6 2
  ./build/one_multi_path tcp://<机器A_IP>:8899 8 7 3
  ```

  其中`<ip:port>`统一指向rank 0所在机器；`<rank_size>`为全局rank总数，`<rank>`为本进程的全局
  rank，`<device>`为本机Device ID。

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  日志顺序可能因多进程并发而不同。每个rank都应完成到其他rank的数据校验，例如：

  ```text
  [rank 0] one_multi_path | channels_per_peer=2, channel_descs=2
  rank 0: peer=1, rank_graph_topo=5, protocol=UB_MEM, path_modes=1/2
  rank 0: dual-path data copy from remote rank 1 passed, bytes=4096
  [rank 0] one_multi_path | dual-path validation | PASS
  ```

  所有rank均输出`PASS`且最终输出`RESULT | Example=one_multi_path | Status=PASS`，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 默认场景需要Device 0和Device 1可用，且两个Device之间存在`COMM_PROTOCOL_UB_MEM`链路；出现
  `no UB_MEM link found`时，表示当前rank与目标rank之间不存在可用的`COMM_PROTOCOL_UB_MEM`链路。
- `rank_graph_topo`日志只显示所选UB_MEM链路所在的RankGraph层，不参与`pathMode`选择。
- 多机运行时，所有机器必须使用相同的`<rank_size>`与`<ip:port>`（指向rank 0），rank范围必须
  完整且不能重复。
- `<ip:port>`控制通道只用于分发`HcclRootInfo`与Host侧同步，不参与HCCL数据链路选择。
- `HcclChannelAcquire`失败时，可根据前序`peer`链路筛选日志定位失败范围。
