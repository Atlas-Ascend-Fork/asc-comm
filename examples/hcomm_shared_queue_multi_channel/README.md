# Hcomm 多 Channel 共享队列样例

## 概述

本样例展示如何基于 `HcclRootInfo` 创建多卡通信域，并通过 `HcclChannelAcquireWithConfig` 让同一本地 Endpoint 下的多个逻辑 Channel 共享通信队列。Host 侧完成通信内存注册、RankGraph 查询、Channel 分组和创建；AIV Kernel 依次使用这些 Channel 向不同 peer 写入数据，并在 Host 侧校验结果。

rank 0 生成 `HcclRootInfo` 并通过 TCP 分发给其他 rank，各 rank 使用同一份 `HcclRootInfo` 初始化通信域。

## 支持的产品及版本

| 产品 | CANN 软件版本 |
|------|---------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## 目录结构

```text
├── hcomm_shared_queue_multi_channel
│   ├── CMakeLists.txt                          // CMake 构建文件
│   ├── README.md                               // 中文说明
│   ├── README_en.md                            // 英文说明
│   ├── hcomm_shared_queue_multi_channel.asc    // AIV Kernel 及启动封装
│   ├── hcomm_shared_queue_multi_channel.cpp    // Host 控制面及结果校验
│   └── run.sh                                  // 多进程编译运行脚本
```

## 样例说明

### 资源模型

```text
本地 rank
  Endpoint group 0
    Channel(peer A) --+
    Channel(peer B) --+-- shared queue group 0

  Endpoint group 1
    Channel(peer C) ----- queue group 1
```

控制面从 RankGraph 中为每个 peer 选择最近层的一条 `COMM_PROTOCOL_UBC_CTP` link，并按照 link 的本地 Endpoint 分组。Endpoint 是否相同由 `protocol`、`commAddr.type` 和 `commAddr.eid` 共同确定。

- 同一 group 的所有 `HcclChannelDesc` 具有相同的 `localEndpoint`，通过一次 `HcclChannelAcquireWithConfig` 批量创建并共享通信队列。
- 每个本地 Endpoint group 分别调用一次 `HcclChannelAcquireWithConfig`。

共享队列的关键配置如下：

```cpp
HcclChannelConfig config = nullptr;
HcclChannelConfigCreate(&config);
HcclChannelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1);
HcclChannelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, sharedTag);
HcclChannelAcquireWithConfig(
    comm, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
HcclChannelConfigDestroy(config);
```

### 执行流程

```text
rank 0 调用 HcclGetRootInfo
  -> TCP 分发同一份 HcclRootInfo
  -> 各 rank 调用 HcclCommInitRootInfo
  -> HcclCommMemReg 注册接收区和 notify
  -> 查询 RankGraph，为每个 peer 选择 UBC_CTP link
  -> 按 localEndpoint 对 Channel descriptor 分组
  -> 每个 group 创建一组共享通信队列的逻辑 Channel
  -> 分两轮依次使用本 rank 到所有 peer 的 Channel
  -> 每轮等待远端 notify 并校验接收数据
```

Kernel 使用 `COMM_PROTOCOL_UBC_CTP`，控制面使用 `COMM_ENGINE_AIV`。Kernel 调用 `Lock` 获取共享队列锁并加载队列索引，在 `WriteWithNotifyNbi<false>`、`Commit` 和 `Drain` 完成后调用 `Unlock` 写回索引并释放锁。Kernel 将执行结果写回 Host，远端 Host 轮询注册内存中的 notify 并校验数据。

每轮数据传输前后调用 `HcclBarrier`，确保所有 rank 已经清零接收区和 notify。

### 多 Channel 验证方式

样例创建本 rank 到所有 peer 的 Channel，并执行两轮 all-peer 数据面验证。每轮按统一 offset 调度：rank `i` 写入 `(i + offset) % rankSize`，同时接收 `(i - offset + rankSize) % rankSize` 的数据。当存在共享队列时，同一 group 内的多个 Channel 交替使用，覆盖共享通信队列上的逻辑 Channel 切换。

## 编译运行

### 1. 配置环境

```bash
source ${install_path}/cann/set_env.sh
```

`${install_path}` 为 CANN 安装目录，默认安装路径通常为 `/usr/local/Ascend`。

### 2. 单机多卡运行

进入样例目录执行：

```bash
cd examples/hcomm_shared_queue_multi_channel
bash run.sh
```

`run.sh` 的参数如下：

| 参数 | 说明 |
|------|------|
| `$1` | 全局 rank 数量，默认 `2` |
| `$2` | 单次通信字节数，默认 `4096` |
| `$3` | rank 0 分发 RootInfo 的 TCP 地址，默认 `tcp://127.0.0.1:8899` |
| `$4` | 当前机器启动的第一个全局 rank，默认 `0` |
| `$5` | 当前机器启动的 rank 数量，默认等于 `$1` |

例如运行 4 卡：

```bash
bash run.sh 4 4096
```

单机运行时，全局 rank 与本机 device 编号相同。也可以手动编译并启动：

```bash
cmake -S . -B build -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build -j

./build/demo 2 0 0 4096 &
./build/demo 2 1 1 4096 &
wait
```

`demo` 参数为 `<rank_size> <rank> <device> <bytes> [ipport]`。

### 3. 多机运行

各机器使用相同的 `rank_size`、通信字节数、RootInfo 地址和程序版本，并通过 `run.sh` 的后两个参数指定本机负责的全局 rank 区间。本机 device 从 `0` 开始映射。

例如两台机器各运行两个 rank，rank 0 所在机器的 IP 为 `192.168.1.10`：

```bash
# 机器 A：全局 rank 0-1
bash run.sh 4 4096 tcp://192.168.1.10:8899 0 2

# 机器 B：全局 rank 2-3
bash run.sh 4 4096 tcp://192.168.1.10:8899 2 2
```

建议先启动包含 rank 0 的机器，其他 rank 会在限定时间内重试连接 rank 0。

### 4. 预期输出

各 rank 完成两轮数据校验后，每个 rank 输出一行结果：

```text
rank 0 pass, exercised 1 peer channels for 2 cycles, bytes 4096.
rank 1 pass, exercised 1 peer channels for 2 cycles, bytes 4096.
ranks 0-1 run success.
```

## 运行约束

- `rank_size` 必须大于等于 2。
- 同一 local Endpoint 下的多个 Channel 会共享通信队列；RankGraph 返回的 link 按本地 Endpoint 分组，同一 group 内的 Channel 共享通信队列。
- 跨节点运行时，各机器的全局 rank 区间不能重叠，并且必须完整覆盖 `0..rank_size-1`。
- 所有 rank 必须使用相同的 RootInfo、rank 数量和程序版本。
- 实际运行需要可用的 CANN、HCCL 和 NPU 环境。
