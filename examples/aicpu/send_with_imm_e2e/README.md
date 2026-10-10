# AICPU Send With Immediate 端到端样例

## 概述

本样例验证AICPU数据面接口`asccomm_send_with_imm_on_thread`的完整双边通信链路：client侧在自定义AICPU
kernel中提交携带immediate data的SEND，server侧创建host URMA channel、注册接收内存、预投递receive buffer，
并通过`HcommChannelPollJfc`校验接收CQE和payload。

样例会校验每条消息的接收完成、`ok`、`isRecv`、`hasImmData`、`immData`、`byteLen`和payload字节序列。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
send_with_imm_e2e
├── CMakeLists.txt              // 编译工程文件
├── README.md                   // 中文样例说明
├── README_en.md                // 英文样例说明
├── common/send_with_imm_e2e.h  // 测试协议和参数定义
├── client/main.cpp             // client侧资源创建和AICPU kernel启动
├── server/main.cpp             // server侧接收资源管理和CQE校验
├── kernel/send_with_imm_kernel.cc // 自定义AICPU通信kernel
├── scripts/run.sh              // 一键构建和运行脚本
└── send_with_imm_aicpu.json.in // AICPU kernel元数据模板
```

## 样例描述

### 功能说明

1. server查询host endpoint，创建TCP控制连接，并创建`COMM_ENGINE_CPU` channel。
2. server分配host接收内存，调用`HcommMemReg`注册，按接收槽位调用`HcommChannelPostRecv`预投递，
   并把接收内存描述符发送给client。
3. client查询device endpoint，导入server内存描述符，创建`COMM_ENGINE_AICPU_TS` channel，
   分配并注册device发送内存。
4. client调用`HcommChannelBuildDeviceEntity`获取AICPU可用的channel句柄，通过ACL runtime加载
   `send_with_imm_aicpu.json`并启动自定义AICPU kernel。
5. kernel调用`asccomm_send_with_imm_on_thread(thread, channel, src, len, immData)`发送数据。
6. server调用`HcommChannelPollJfc`等待接收完成，校验CQE字段和payload，并把统计结果回传client。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | AICPU到host的URMA双边SEND |
| AICPU接口 | `asccomm_send_with_imm_on_thread` |
| 通信引擎 | client `AICPU_TS`，server `CPU` |
| 消息数量 | `SEND_COUNT`，默认1 |
| 单条消息长度 | `PAYLOAD_BYTES`，默认64字节 |
| immediate data | `IMM_DATA`，默认305419896（0x12345678） |
| 接收槽位 | `RECV_SLOTS`，默认8 |

## 编译运行

### 前置条件

- 已source CANN环境变量：

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

- CANN环境中已安装包含`HcommChannelPostRecv`、`HcommChannelPollJfc`和`HcommChannelBuildDeviceEntity`的最新hcomm包，以及包含AICPU头文件和`libasccomm_aicpu.so`的asc-comm开发验证包。先构建并安装最新hcomm包；若asc-comm包未安装，在仓库根目录生成并安装：

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_<version>_linux-<arch>.run --full
  ```

### 运行

在样例目录执行：

```bash
bash scripts/run.sh
```

脚本默认构建到`build/`，随后启动server和client。已构建过且只修改运行参数时，可使用：

```bash
bash scripts/run.sh --skip-build
```

跨主机运行时，server侧只启动server，client侧设置server地址后运行脚本；两侧需要使用相同的TCP端口和测试参数。

### 参数说明

| 环境变量 | 默认值 | 说明 |
| --- | ---: | --- |
| `SERVER_TCP_PORT` | 49152 | TCP控制面端口 |
| `CLIENT_HOST` | 127.0.0.1 | server IP地址 |
| `DEVICE_ID` | 0 | client使用的NPU设备 |
| `PAYLOAD_BYTES` | 64 | 单条消息字节数 |
| `IMM_DATA` | 305419896 | 32-bit immediate data |
| `SEND_COUNT` | 1 | 发送消息数量 |
| `RECV_SLOTS` | 8 | server预投递接收槽位数量 |
| `BUILD_JOBS` | nproc | 构建并发数 |

### 执行结果

运行成功时输出类似：

```text
[send-imm-server] listening on port 49152
[send-imm-server] received=1 errors=0 expected=1
[send-imm-client] received=1 errors=0 apiReturn=0 expected=1
```

client和server退出码均为0表示样例通过。

## 注意事项

- 本样例是真机AICPU/URMA链路验证，当前开发环境没有NPU时只能验证构建，不能运行数据面。
- `build/send_with_imm_aicpu.json`会在配置阶段写入自定义AICPU SO的绝对路径；移动构建目录后需要重新配置。
- 运行环境需要支持ACL runtime按该绝对路径加载自定义AICPU动态库。
- `SEND_COUNT`可以大于1；server会循环接收并复用已完成槽位。
- 通信channel使用固定端口60001，同一组主机上并行运行多个实例时需要避免端口冲突。

