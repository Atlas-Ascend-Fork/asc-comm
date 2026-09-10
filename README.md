<div align="center">

# asc-comm

<h4>面向昇腾AI处理器通信场景，提供Hcomm与Ain通信API、AIV直驱实现、样例和验证用例</h4>

[![docs](https://img.shields.io/badge/docs-repo-blue.svg?style=flat)](./docs)
[![examples](https://img.shields.io/badge/examples-repo-orange.svg?style=flat)](./examples)
[![license](https://img.shields.io/badge/license-CANN_Open_2.0-lightgrey.svg)](./LICENSE)
[![contributing](https://img.shields.io/badge/CONTRIBUTING-teal)](./CONTRIBUTING.md)

</div>

## 🔥Latest News

- [2026/07] asc-comm项目首次上线

### 🚀 当前能力

- 提供AICore侧Hcomm点对点通信接口，覆盖普通`Init`、`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`、`AtomicCAS`、`Commit`、`Drain`，以及Ascend 950 UBC_CTP路径的`MakeBatchHandle`、批量读写、`BatchCommit`和批量`Drain`。
- 提供AIV直驱Hcomm RoCE和UBC_CTP/URMA相关实现，主实现位于`src/aicore/hcomm/`。
- 提供AICore侧Ain单边通信接口，覆盖`Put`、`PutValue`、`Get`、`Signal`、`ReadSignal`、`WaitSignal`、`Flush`、`FlushAsync`、`Wait`，以及`AinBarrierSession`集合通信同步原语，主实现位于`src/aicore/ain/`。
- 提供Hcomm UT工程，覆盖`ascend950pr_9599_AIV`的RoCE/URMA普通接口和UBC_CTP批量接口，以及`ascend910B1_AIC`基础接口用例。
- 提供Ain UT工程，覆盖`ascend950pr_9599_AIV`的URMA路径下`Put`/`Get`/`Signal`/`ReadSignal`/`WaitSignal`/`BarrierSession`接口用例。
- 提供`hcomm_write_read_nbi`样例，演示AIV直驱URMA场景下`WriteNbi`和`ReadNbi`点对点通信流程，并包含运行样例所需的Host侧资源准备流程。
- 提供SIMT URMA `WriteNbi`、`WriteValueNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`AtomicFAA`、`AtomicCAS`和`Drain`，以及`simt_urma`功能样例和`simt_urma_perftest`性能样例。

### 📖 资料文档

- 新增[快速开始](./docs/quick_start.md)、[构建与测试](./docs/zh/guide/build_and_test.md)、[三方依赖与兼容性](./docs/zh/guide/dependencies.md)说明。
- 新增[Hcomm使用说明](./docs/zh/guide/hcomm_usage.md)和[API参考](./docs/zh/api/README.md)，覆盖当前公开的Hcomm接口。
- 新增[样例目录](./examples/README.md)，提供Hcomm AIV直驱调用和端到端通信样例入口。

有关所有历史版本及更新的详细信息，请参阅[CHANGELOG.md](./CHANGELOG.md)。

## 🚀概述

asc-comm是面向昇腾AI处理器通信场景的开源仓，当前用于承载AICore侧公开API、AIV直驱设备侧实现、API文档、样例和验证能力。

当前公开能力包括`AscendC::Hcomm`点对点通信和`AscendC::Ain`单边通信，面向算子Kernel侧通信数据路径。Hcomm侧使用方通过`AscendC::Hcomm`模板选择通信协议：普通接口通过`ChannelHandle`逐条提交通信任务；Ascend 950 UBC_CTP路径还可以通过BatchHandle在UB中批量准备WQE，并通过`BatchCommit`统一提交。两种流程分别通过对应的`Drain`重载管理完成等待。Ain侧使用方通过`AscendC::Ain`模板基于对称窗口（Symmetric Window）发起`Put`/`Get`/`Signal`等单边操作，通过`Flush`或`FlushAsync`+`Wait`管理完成等待。

### 数据面能力

| 能力 | 当前状态 |
| --- | --- |
| AICore Hcomm公开接口 | 已提供Kernel侧普通`Init`、读写、写通知、原子、`Commit`和`Drain`接口；Ascend 950 UBC_CTP路径还提供`MakeBatchHandle`、批量`ReadNbi`/`WriteNbi`/`WriteWithNotifyNbi`、`BatchCommit`和批量`Drain`。 |
| AICore Ain公开接口 | 已提供Kernel侧`Put`、`PutValue`、`Get`、`Signal`、`ReadSignal`、`WaitSignal`、`Flush`、`FlushAsync`、`Wait`，以及`AinBarrierSession`同步原语。 |
| AIV直驱实现 | 已提供Hcomm RoCE和UBC_CTP/URMA相关实现，主实现位于`src/aicore/hcomm/`；Ain实现位于`src/aicore/ain/`。 |
| AIV直驱样例配套流程 | `hcomm_write_read_nbi`包含AIV直驱URMA通信所需的通信域创建、通信内存注册、P2P通道创建和远端内存获取流程。 |
| 协议能力 | `COMM_PROTOCOL_ROCE`支持普通读写、提交和等待；`COMM_PROTOCOL_UBC_CTP`支持普通读写、写通知、原子操作、提交和等待，Ascend 950还支持批量读写、写通知、提交和等待。 |
| UT验证 | UT覆盖`ascend950pr_9599_AIV`的Hcomm RoCE/URMA普通接口、UBC_CTP批量接口与Ain URMA路径，以及`ascend910B1_AIC`基础接口用例。 |
| AIV直驱样例 | 提供`hcomm_write_read_nbi`样例，覆盖两卡AIV直驱URMA `WriteNbi`/`ReadNbi`对称通信和结果校验流程。 |
| SIMT URMA接口 | 提供`WriteNbi`、`WriteValueNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`AtomicFAA`、`AtomicCAS`和`Drain`；延迟任务由后续`commit=true`任务统一发布。 |
| SIMT URMA样例 | 提供`simt_urma`功能样例，覆盖全部五个接口的单接口、连续提交和batch-last提交；提供`simt_urma_perftest`性能样例，统计下发时延与完成带宽。 |

### 如何使用Hcomm接口

Hcomm Kernel侧使用时包含如下头文件：

```cpp
#include "hcomm/hcomm.h"
```

普通接口调用流程如下：

1. 创建`AscendC::Hcomm`对象，并选择通信协议。
2. 调用`Init`初始化临时工作区。
3. 通过`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`或`AtomicCAS`提交通信任务。
4. 如果提交任务时设置`commit = false`，调用`Commit`显式提交通信任务。
5. 调用`Drain`等待通道上的通信任务完成。

Ascend 950 UBC_CTP批量接口调用流程如下：

1. 准备UB缓冲区，通过`MakeBatchHandle`创建批量句柄。该流程不依赖`Init`。
2. 通过BatchHandle重载的`ReadNbi`、`WriteNbi`或`WriteWithNotifyNbi`在UB中准备WQE，同一批次可以混合三种任务。
3. 调用`BatchCommit`将当前批次复制到GM SQ并敲doorbell。
4. 可以复用句柄继续准备和提交批次，最后调用BatchHandle重载的`Drain`等待CQE。

BatchHandle缓存创建时的SQ/CQ上下文和队列状态，使用期间调用方需要独占对应单通道或共享Jetty，不能混用普通接口。单通道`MakeBatchHandle`根据`remoteAddr`查找并缓存远端MR的token；共享Jetty模式由`GetHandleRef`根据逻辑通道和`remoteAddr`完成该查找。后续批量读写不会再次查询MR表，调用方必须保证远端访问使用本次缓存的token。

协议能力说明：

| 协议 | 能力说明 |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | RoCE点对点通信路径，支持普通`ReadNbi`、`WriteNbi`、`Commit`、`Drain`，不支持`WriteWithNotifyNbi`和BatchHandle接口。 |
| `COMM_PROTOCOL_UBC_CTP` | UBC CTP/URMA点对点通信路径，支持普通`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`、`AtomicCAS`、`Commit`、`Drain`；Ascend 950还支持BatchHandle批量接口。 |

详细参数约束和返回值说明请参考[Hcomm使用说明](./docs/zh/guide/hcomm_usage.md)和[API参考](./docs/zh/api/README.md)。

### 如何使用Ain接口

Ain Kernel侧使用时包含如下头文件：

```cpp
#include "ain/ain.h"
```

基本调用流程如下：

1. 创建`AscendC::Ain`对象，绑定通信上下文索引。
2. 通过`Put`/`PutValue`/`Get`发起单边读写，或通过`Signal`发起远端原子信号操作。
3. 如果提交任务时设置`AIN_COMMIT_DELAYED`，提交将被延迟，直到后续`AIN_COMMIT_IMMED`任务触发敲门铃；否则立即提交。
4. 完成等待可通过`Flush`或`FlushAsync`+`Wait`管理。
5. 通过`ReadSignal`/`WaitSignal`读取或等待本地信号。
6. 如需集合同步，通过`AinBarrierSession`的`Sync`完成同步。

提交模式说明：

| 模式 | 行为说明 |
| --- | --- |
| `AIN_COMMIT_IMMED` | 组装通信任务后立即敲响门铃，提交给底层引擎执行。 |
| `AIN_COMMIT_DELAYED` | 仅组装通信任务，不敲门铃；延迟到后续`AIN_COMMIT_IMMED`任务触发提交。 |

详细参数约束和返回值说明请参考[API参考](./docs/zh/api/README.md)。

## 🔍目录结构说明

本仓主要包含asc-comm AICore侧通信数据面API、设备侧实现、样例、文档和UT用例，目录结构如下：

```text
├── cmake                         # asc-comm CMake辅助模块
├── docs                          # 项目文档介绍
├── examples                      # asc-comm API样例目录
│   ├── hcomm_write_read_nbi      # Hcomm AIV直驱URMA两卡P2P通信样例
│   ├── simt_urma                 # Hcomm SIMT URMA功能样例
│   └── simt_urma_perftest        # Hcomm SIMT URMA性能样例
├── include                       # asc-comm API声明源代码
│   ├── aicore/hcomm              # AICore侧Hcomm公开接口
│   └── aicore/ain                # AICore侧Ain单边通信公开接口
├── scripts                       # 脚本
├── src                           # asc-comm API实现源代码
│   ├── aicore/hcomm              # AICore侧Hcomm实现细节
│   │   ├── common                # Hcomm公共定义和工具
│   │   └── impl                  # Hcomm协议实现与平台差异代码
│   └── aicore/ain                # AICore侧Ain实现细节
│       └── impl                  # Ain单边通信原语实现
└── tests                         # asc-comm API UT用例
    └── ut/aicore
        ├── hcomm                 # AICore Hcomm UT工程
        └── ain                   # AICore Ain UT工程
```

## ⚡️快速入门

若您希望快速体验项目构建和UT验证，请先配置CANN环境：

```bash
source /usr/local/Ascend/cann/set_env.sh
```

默认构建用于检查基础环境。当前AICore Hcomm与Ain代码以头文件形式集成，非UT构建不会生成独立库：

```bash
bash build.sh
```

默认构建会复用已有的`build/`目录，不会自动删除构建产物。如需清理构建目录，请显式执行：

```bash
bash build.sh --make_clean
```

构建并运行Hcomm与Ain UT：

```bash
bash build.sh -t
```

将仓库中的asc-comm头文件制作为开发验证run包，并安装到已有CANN环境：

```bash
bash build.sh --pkg
./build_out/cann-asc-comm_9.2.0_linux-<arch>.run --full
```

制包脚本会递归收集`include/`和`src/`中的头文件，其中Hcomm安装到`adv_api`，其他头文件按相对路径安装到`comm_api`。安装目录、命令参数、权限和卸载说明见[构建与测试](./docs/zh/guide/build_and_test.md)。

如需直接使用CMake构建UT，可指定CANN三方依赖目录：

```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<third_party_path>
cmake --build build/ut-hcomm
```

更多环境准备、Docker、CANN包安装和UT依赖说明请参考[快速开始](./docs/quick_start.md)和[构建与测试](./docs/zh/guide/build_and_test.md)。

## 🧰clangd/IDE支持

- 安装clangd，推荐使用15或以上版本。
- 配置本地IDE时，需要将CANN头文件目录和本仓`include/`目录加入索引路径。
- 在修改Hcomm Kernel侧代码前，建议先`source /usr/local/Ascend/cann/set_env.sh`，确保CANN相关环境变量已配置。
- 如果使用VS Code，可结合C/C++、clangd等插件完成代码跳转、语法检查和头文件索引。

## 📖相关资源

- **文档**

  | 文档 | 说明 |
  | --- | --- |
  | [文档入口](./docs/README.md) | asc-comm文档总入口。 |
  | [快速开始](./docs/quick_start.md) | 环境准备、源码编译和UT验证。 |
  | [API参考](./docs/zh/api/README.md) | asc-comm当前公开接口列表。 |
  | [Hcomm使用说明](./docs/zh/guide/hcomm_usage.md) | Hcomm点对点通信接口的基本使用流程。 |
  | [构建与测试](./docs/zh/guide/build_and_test.md) | CANN环境、开发验证run包、UT构建和样例构建说明。 |
  | [三方依赖与兼容性](./docs/zh/guide/dependencies.md) | 本仓直接依赖、样例运行依赖、安装配置和集成依赖边界。 |
  | [样例目录](./examples/README.md) | asc-comm API样例入口。 |

- **贡献指南**

  | 文档 | 说明 |
  | --- | --- |
  | [CANN社区贡献指南](https://gitcode.com/cann/community) | CANN社区Issue、PR等通用处理流程。 |
  | [asc-comm贡献指南](./CONTRIBUTING.md) | 本仓Issue、开发、检查和PR提交流程。 |
  | [API文档贡献指南](./docs/api_contributing.md) | 新增或修改API文档时的结构、约束和检查要求。 |
  | [资料贡献指南](./docs/doc_contributing.md) | README、docs、examples等资料文档的补充规范。 |

- **其他**

  | 文档 | 说明 |
  | --- | --- |
  | [更新日志](./CHANGELOG.md) | 版本变更记录。 |
  | [安全声明](./SECURITY.md) | 安全问题反馈和处理说明。 |
  | [三方开源软件清单](./Third_Party_Open_Source_Software_List.yaml) | 本仓三方开源软件清单。 |
  | [三方开源软件声明](./Third_Party_Open_Source_Software_Notice) | 本仓三方开源软件声明。 |

## 📌相关规划

- 持续补充AIV直驱Hcomm端到端样例，覆盖更多协议路径和通信接口。
- 持续完善不同产品、协议路径下的构建验证和UT覆盖。
- 持续补充API约束、使用说明和常见问题。

## 📝相关信息

- [贡献指南](./CONTRIBUTING.md)
- [安全声明](./SECURITY.md)
- [许可证](./LICENSE)
