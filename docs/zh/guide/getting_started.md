# 快速入门

## 简介与概述

面向大规模NPU集群上大模型训练与推理场景，跨卡通信是集群吞吐的核心瓶颈。预置通用通信算子难以适配各类模型与并行策略的差异化需求，需要定制化通信实现充分释放硬件能力。
asc-comm是面向Device侧的数据面通信底座，对外提供配套API以支撑自定义通信算子开发，帮助开发者专注于通信的数据传输逻辑，屏蔽芯片底层的复杂实现细节。架构上与[hcomm](https://gitcode.com/cann/hcomm/blob/9.3.0/README.md)分工协作：hcomm承担控制面职责，提供拓扑信息查询与通信资源管理功能。

asc-comm提供覆盖AIV、AI CPU+TS、CCU三类通信引擎的数据面通信能力，包含四组核心接口：

- **[AI Core CommRW](../api/aicore/asccomm/README.md)**：AIV直驱的点对点通信原语，提供SIMD和SIMT两种编程模型，覆盖远端读写、远端归约写、原子操作、通知同步与批量提交。
- **[AI Core AIN](../api/aicore/ain/README.md)**：AIV直驱的单边通信接口，基于对称window和通信组team，提供远端读写和信号同步能力。
- **[AI CPU CommRW](../api/aicpu_ts/asccomm/index.md)**：AI CPU侧点对点通信原语，覆盖本地/远端读写与归约、通知同步、屏障和批量传输。
- **[CCU](../api/ccu/index.md)**：CCU引擎的数据面API，提供变量、事件、Buffer等资源抽象与数据搬运、归约、同步原语，支撑通信与计算融合执行。

以 **AIN** 接口为例，带您完成从环境搭建到运行第一个NPU间点对点通信算子的完整流程。

**前置知识**：本指南假设您了解[Ascend C算子开发基础](https://gitcode.com/cann/asc-devkit/blob/9.3.0/docs/zh/guide/index.md)。

## 环境安装与准备<a name="env-setup"></a>

- 进行asc-comm通信算子开发之前，需要安装**驱动固件**和**CANN软件包**，请参考[《CANN快速安装》](https://www.hiascend.com/cann/download)完成环境准备。

    > [!NOTE]说明
    > 安装CANN软件包后，使用CANN运行用户进行编译、运行时，需要以CANN运行用户登录环境，执行`source ${INSTALL_DIR}/set_env.sh`命令设置环境变量。`${INSTALL_DIR}`请替换为CANN软件安装后的文件存储路径。以root用户安装为例，安装后文件默认存储路径为：/usr/local/Ascend/cann。

- 安装CMake。通过CMake编译Ascend C算子时，要求安装3.16及以上版本的CMake，如果版本不符合要求，可以参考如下示例安装满足要求的版本。

  示例：安装3.16.0版本的CMake（x86\_64架构）。

  ```bash
  mkdir -p cmake-3.16 && wget -qO- "https://cmake.org/files/v3.16/cmake-3.16.0-Linux-x86_64.tar.gz" | tar --strip-components=1 -xz -C cmake-3.16
  export PATH=`pwd`/cmake-3.16/bin:$PATH
  ```

## 第一个示例程序：AIN点对点通信<a name="first-example"></a>

本节以AIN接口的[basic_ring](../../../examples/aicore/ain/01_basic_ring/README.md)样例为例，介绍最简单的NPU间点到点通信算子的编写和运行。

### 样例简介

`nranks`个rank组成环形拓扑，每个rank执行两步单边通信：

1. **`put`**：将本rank标识写入下一个rank的`recv_window[rank_id]`
2. **`get`**：从上一个rank的`send_window[0]`读取数据到本rank的`recv_window[rank_num + rank_id]`

通信完成后通过`ain_barrier_session::sync`完成rank间同步，Host侧回读`recv_buf`校验`put`和`get`的结果。

### 核心代码解读

以下为Kernel侧最核心的AIN调用代码：

```cpp
#include "ain/ain.h"

// 1. 构造ain对象（context_index对应使用的通信引擎上下文编号）
asc::ain ain(0);

// 2. 单边写（`put`）：将本卡send_window的1字节数据写到对端recv_window的指定偏移
//    参数依次：team句柄、对端rank号、对端window、对端偏移、本端window、本端偏移、字节数、
//             写后远端动作（`ain_remote_none{}`表示不追加远端动作）、UB工作区描述符
ain.put(ain_team, peer, recv_window, static_cast<uint64_t>(rank_id), send_window, 0U, 1U,
        asc::ain_remote_none{}, asccomm_ubuf);

// 3. 单边读（`get`）：从上一个rank的send_window读到本卡recv_window
ain.get(ain_team, prev, recv_window, get_dst_offset, send_window, 0U, 1U, asccomm_ubuf);

// 4. 等待通信任务完成
ain.flush(ain_team);

// 5. 通过ain_barrier_session完成rank间同步
asc::ain_barrier_session ain_barrier(&ain, ain_team, 0U);
ain_barrier.sync(asc::AIN_MEMORY_ORDER_RELAX, asccomm_ubuf);
```

**要点说明**：

- `asccomm_ubuf`是UB临时工作区描述符，以c_api方式声明UB数组（如`__ubuf__ uint8_t asccomm_ubuf_data[asc::HCOMM_URMA_TMP_BUF_SIZE]`）后构造`asc::ain_descriptor_ubuf`传入。
- `put`/`get`为异步接口，入队后立即返回；`flush`阻塞等待当前引擎上所有已入队任务完成。
- `ain_barrier_session::sync`实现rank间环形同步：signal其他所有rank后wait_signal等全部到达。

### 环境与依赖

- **产品**：Ascend 950PR&950DT系列产品
- **CANN版本**：>= 9.3.0
- **NPU数量**：运行需至少2张；单卡仅支持编译验证
- **通信引擎**：`COMM_ENGINE_AIV`，协议`COMM_PROTOCOL_UB_CTP`

### 编译运行

进入本开源仓代码根目录，下载源码并编译：

```bash
git clone https://gitcode.com/cann/asc-comm.git
cd asc-comm
bash build.sh
```

进入AIN样例目录编译运行：

```bash
source /path/to/cann/set_env.sh
cd examples/aicore/ain/01_basic_ring
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./basic_ring_demo tcp://127.0.0.1:29622 <nranks>
```

执行成功时输出如下：

```text
[rank 0] basic_ring | PUT recv_buf[1]=1, GET recv_buf[2]=1 | PASS
[rank 1] basic_ring | PUT recv_buf[0]=0, GET recv_buf[3]=0 | PASS
RESULT | Example=basic_ring | Status=PASS
```

### 启动参数说明

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换；并行运行多个实例时错开端口 |
| `<nranks>` | 无默认 | 启动的rank数量，必须为不小于2的整数，且不超过当前可用NPU数量 |

### 下一步

- 了解Ascend C通信编程模型与硬件架构基础，参见[编程模型](./programming_model/README.md)。
- 了解各模块API详细信息，参见[API参考](../api/index.md)。
- 了解如何基于各引擎的API实现通信算子，参见[通信算子开发](./communication_op_dev/README.md)。
- 了解各引擎更多通信样例，参见[样例目录](../../../examples/README.md)。
