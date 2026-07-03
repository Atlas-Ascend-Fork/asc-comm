<div align="center">

# asc-comm

<h4>面向昇腾AI处理器通信场景，提供AICore通信API声明、AIV直驱实现和验证用例</h4>

[![docs](https://img.shields.io/badge/docs-repo-blue.svg?style=flat)](./docs)
[![examples](https://img.shields.io/badge/examples-repo-orange.svg?style=flat)](./examples)
[![license](https://img.shields.io/badge/license-CANN_Open_2.0-lightgrey.svg)](./LICENSE)
[![contributing](https://img.shields.io/badge/CONTRIBUTING-teal)](./CONTRIBUTING.md)

</div>

## 🚀概述

asc-comm是面向昇腾AI处理器通信场景的开源仓，当前处于从asc-devkit迁移通信能力的阶段。仓内已纳入一部分AIV直驱Hcomm代码，主要提供AICore侧点对点通信API声明、设备侧实现、API文档和UT验证。

当前公开能力以`AscendC::Hcomm`为主，使用方通过`AscendC::Hcomm`模板选择通信协议，并通过`ChannelHandle`指定通信通道。接口支持提交点对点读写任务，并通过`Commit`和`Drain`控制任务提交与完成等待。

> [!NOTE]
> 本仓只描述当前已纳入构建、文档和UT验证流程的能力。`hcomm_common.h`中保留的其他通信协议枚举不代表当前均已公开支持。若本地存在`asc-devkit/`、`hcomm/`等迁移参考目录，请以本仓已跟踪的`include/`、`src/`、`docs/`和`tests/`内容为准。

## 🔥当前能力

| 能力 | 当前状态 |
| --- | --- |
| AICore Hcomm公开接口 | 已提供`Init`、`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`Commit`、`Drain`。 |
| AIV直驱实现 | 已迁移Hcomm RoCE和UBC_CTP/URMA相关实现，代码位于`include/aicore/hcomm/detail/`和`src/direct_drive/device/`。 |
| 协议能力 | `COMM_PROTOCOL_ROCE`支持读写、提交和等待；`COMM_PROTOCOL_UBC_CTP`支持读写、写通知、提交和等待。 |
| UT验证 | UT覆盖`ascend950pr_9599_AIV`的RoCE/URMA路径，以及`ascend910B1_AIC`基础接口用例。 |
| 样例 | `examples/`目录已预留，端到端样例仍在补充中。 |

## 🔍目录结构说明

本仓主要包含asc-comm API、设备侧实现、文档和UT用例，目录结构如下：

```text
├── docs                          # 项目文档介绍
├── examples                      # asc-comm API样例目录，当前预留
├── include                       # asc-comm API声明源代码
│   ├── aicore/hcomm              # AICore侧Hcomm公开接口与头文件实现
│   ├── ain                       # AIN相关API预留目录
│   └── direct_drive              # Direct Drive相关API预留目录
├── scripts                       # 脚本
├── src                           # asc-comm API实现源代码
│   └── direct_drive/device       # AIV Direct Drive设备侧实现
└── tests                         # asc-comm API UT用例
    └── ut/aicore/hcomm           # AICore Hcomm UT工程
```

## ⚡️快速入门

执行构建前，需要先配置CANN环境：

```bash
source /usr/local/Ascend/cann/set_env.sh
```

默认构建用于检查基础环境。当前AICore Hcomm代码以头文件形式集成，非UT构建不会生成独立库：

```bash
bash build.sh
```

构建并运行Hcomm UT：

```bash
bash build.sh -t
```

如需直接使用CMake构建UT，可指定CANN三方依赖目录：

```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<third_party_path>
cmake --build build/ut-hcomm
```

更多环境准备、Docker、CANN包安装和UT依赖说明请参考[快速开始](./docs/quick_start.md)和[构建与测试](./docs/guide/build_and_test.md)。

## 🧩Hcomm接口入口

Hcomm Kernel侧使用时包含如下头文件：

```cpp
#include "hcomm/hcomm.h"
```

基本调用流程如下：

1. 创建`AscendC::Hcomm`对象，并选择通信协议。
2. 调用`Init`初始化临时工作区。
3. 通过`ReadNbi`、`WriteNbi`或`WriteWithNotifyNbi`提交通信任务。
4. 如果提交任务时设置`commit = false`，调用`Commit`显式提交通信任务。
5. 调用`Drain`等待通道上的通信任务完成。

协议能力说明：

| 协议 | 能力说明 |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | RoCE点对点通信路径，支持`ReadNbi`、`WriteNbi`、`Commit`、`Drain`，不支持`WriteWithNotifyNbi`。 |
| `COMM_PROTOCOL_UBC_CTP` | UBC CTP/URMA点对点通信路径，支持`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`Commit`、`Drain`。 |

详细参数约束和返回值说明请参考[Hcomm使用说明](./docs/guide/hcomm_usage.md)和[API参考](./docs/api/README.md)。

## 📖相关资源

- **文档**

  | 文档 | 说明 |
  | --- | --- |
  | [文档入口](./docs/README.md) | asc-comm文档总入口。 |
  | [快速开始](./docs/quick_start.md) | 环境准备、源码编译和UT验证。 |
  | [API参考](./docs/api/README.md) | asc-comm当前公开接口列表。 |
  | [Hcomm使用说明](./docs/guide/hcomm_usage.md) | Hcomm点对点通信接口的基本使用流程。 |
  | [构建与测试](./docs/guide/build_and_test.md) | CANN环境、构建脚本和UT构建说明。 |
  | [三方依赖与兼容性](./docs/guide/dependencies.md) | 本仓直接依赖、安装配置和集成依赖边界。 |
  | [样例目录](./examples/README.md) | asc-comm API样例入口，当前预留。 |

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

## 📝相关信息

- [贡献指南](./CONTRIBUTING.md)
- [安全声明](./SECURITY.md)
- [许可证](./LICENSE)
