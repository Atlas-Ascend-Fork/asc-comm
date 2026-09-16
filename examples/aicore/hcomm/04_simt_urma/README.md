# Hcomm SIMT URMA 功能样例

## 概述

本样例验证`hcomm/hcomm_simt.h`中全部SIMT URMA点对点通信接口：`WriteNbi`、`WriteValueNbi`、
`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`，并覆盖立即提交和延迟提交两种发布方式。

拓扑为环形：每个rank把操作发送到`next = (rank + 1) % nranks`的注册内存，并校验
`prev = (rank - 1 + nranks) % nranks`写入的结果。每个rank既是发送方也是接收方，两侧都校验：
`sendBuf`保存本rank原子操作取回的旧值，`recvBuf`保存prev写入的数据，环上每个`recvBuf`恰好只有
一个写者，因此期望值都是确定的。

所有操作均由单个SIMT线程发起。一个channel必须由一个lane驱动：延迟提交的WQE停留在发送队列中，
由后续`commit=true`的提交发布覆盖整批PI，因此同一批任务必须由同一个lane提交。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
simt_urma
├── CMakeLists.txt          // 编译工程文件
├── main.cpp                // Host侧通信域、内存注册和校验流程
├── op_kernel.cpp           // SIMT Kernel侧URMA接口调用
├── simt_urma_common.h      // Host与Kernel共享定义
├── README.md               // 中文样例说明
└── README_en.md            // 英文样例说明
```

## 样例描述

### 功能说明

样例提供十种测试模式，覆盖五个接口各自的立即提交与延迟提交：

**Write系列**

`WriteNbi`从本地缓冲区取数据，WQE中是指向该缓冲区的SGE。第`i`个slot携带值`i + 1`，因此接收侧
任何错值都能直接定位到它本该来自哪个slot。

`WriteValueNbi`把待写值直接内联在WQE中，不读发送缓冲区。要求`config`开启inline（默认即
`URMA_INLINE_CFG`），且`sizeof(T)`不超过WQE内联负载区，两者均有编译期检查。

| `mode` | 提交方式 |
| --- | --- |
| `write_single` | 32次独立的`WriteNbi<commit=true>` |
| `write_batch_last` | 31次串行`WriteNbi<false>`，最后1次`WriteNbi<true>` |
| `write_value_single` | 32次独立的`WriteValueNbi<commit=true>` |
| `write_value_batch_last` | 31次串行`WriteValueNbi<false>`，最后1次`WriteValueNbi<true>` |

**Notify/Atomic系列**

| `mode` | 说明 |
| --- | --- |
| `notify` | 提交一次Notify操作并调用`Drain` |
| `faa` | 提交一次FAA操作，校验远端累加结果和本地取回的旧值 |
| `cas` | 提交一次CAS操作，校验远端交换结果和本地取回的旧值 |
| `single`（默认） | 串行提交Notify、FAA和CAS，每次操作后分别调用`Drain` |
| `batch_last` | Notify和FAA使用`commit=false`延迟提交，最后由`commit=true`的CAS提交整批 |
| `notify_immediate_repeat` | 单lane连续提交`kNotifyImmediateRepeatCount`个`WriteWithNotifyNbi<true>`（当前为100），最后统一调用一次`Drain` |

两个系列使用互不重叠的slot布局，缓冲区按两者中较大的布局分配。SIMT没有`Commit`接口，延迟提交
的任务只能由后续一次`commit=true`的提交带出；所有模式最后都调用`Drain`，确保kernel返回前CQ已被
消费。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | 环形拓扑SIMT URMA点对点通信 |
| 调用方式 | SIMT Kernel内直接调用URMA接口 |
| 测试模式 | `write_single`、`write_batch_last`、`write_value_single`、`write_value_batch_last`、`notify`、`faa`、`cas`、`single`、`batch_last`、`notify_immediate_repeat` |
| 支持rank数 | 任意 >= 2 |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info
   并创建通信域。
2. 每个rank通过`HcclCommMemReg`注册`sendBuf`和`recvBuf`。
3. 通过`HcclRankGraphGetLayers`/`HcclRankGraphGetLinks`获取到`next`的链路Endpoint，调用
   `HcclChannelAcquire`创建Channel，并通过`HcclChannelGetRemoteMems`获取对端内存地址。
4. Kernel侧按选定模式提交URMA操作，最后调用`Drain`等待完成。
5. Host侧通过控制通道的两次barrier（`ready`、`sent`）保证所有rank完成注册和写入后再回读校验。
6. 校验`sendBuf`取回值和`recvBuf`落地数据，输出PASS/FAIL。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  样例编译时使用已安装到CANN目录的asc-comm头文件。若尚未安装，先在仓库根目录生成并安装开发验证
  run包，参数说明参见[`docs/zh/guide/build_and_test.md`](../../../../docs/zh/guide/build_and_test.md)：

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --full
  ```

  然后加载CANN环境变量：

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  在本样例目录下执行如下命令。可执行文件内部fork出全部rank进程，直接运行即可：

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/simt_urma tcp://127.0.0.1:29620 <nranks> [mode]
  ```

- 启动参数说明

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 通信域rank数，必须为不小于2的整数 |
  | `[mode]` | `single` | 测试模式，取值见样例描述；无法识别的mode会直接报错，不会静默回退 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  以2卡运行`write_single`为例，每个rank打印一组结果：

  ```text
  [rank 0] simt_urma write_single | sent to rank 1, received from rank 1 | PASS
  [rank 1] simt_urma write_single | sent to rank 0, received from rank 0 | PASS
  RESULT | Example=simt_urma Mode=write_single | Status=PASS
  ```

  所有rank均输出PASS且最终输出`Status=PASS`，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- Kernel侧通过`AscendC::simt::LocalBufferAddr`和`RemoteBufferAddr`解析缓冲区基址时，传入的是
  通道缓冲区表的下标，而不是`memHandles`数组下标：本地表下标0由CCL buffer预留，第一个显式注册的
  缓冲区使用下标1；远端缓冲区下标通过`HcclChannelGetRemoteMems`按tag查找。直接传`memHandles`
  下标会解析到CCL buffer，此时CQE正常返回但接收方看到的是CCL buffer内容而非payload。
- Host侧`sent` barrier是校验结果可信的关键：没有它，某rank会在写入方kernel尚未完成时读到清零的
  缓冲区。
- 可执行文件捕获`SIGINT`/`SIGTERM`并终止全部rank进程，Ctrl+C不会留下阻塞在Host barrier上、
  仍占用设备的进程。
- 编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
