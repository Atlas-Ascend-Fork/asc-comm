<div align="center">

# asc-comm

<h4>面向昇腾AI处理器通信场景，提供通信API声明与实现</h4>

[![docs](https://img.shields.io/badge/docs-repo-blue.svg?style=flat)](./docs)
[![examples](https://img.shields.io/badge/examples-repo-orange.svg?style=flat)](./examples)
[![license](https://img.shields.io/badge/license-CANN_Open_2.0-lightgrey.svg)](./LICENSE)
[![contributing](https://img.shields.io/badge/CONTRIBUTING-teal)](./CONTRIBUTING.md)

</div>

## 🚀概述

asc-comm提供面向昇腾AI处理器通信场景的API声明、实现、UT用例和样例说明。

## 🔍目录结构说明

本仓主要包含asc-comm API、文档、样例和UT用例，其目录结构如下：

```
├── docs                          # 项目文档介绍
├── examples                      # asc-comm API样例
├── include                       # asc-comm API声明源代码
│   ├── aicore                    # AICore侧API声明
│   ├── ain                       # AIN相关API声明
│   └── direct_drive              # Direct Drive相关API声明
├── scripts                       # 脚本
├── src                           # asc-comm API实现源代码
│   └── direct_drive              # Direct Drive相关API实现
└── tests                         # asc-comm API UT用例
    └── ut                        # UT工程
```

## ⚡️快速入门

若您希望快速体验项目的构建和UT验证，请访问如下文档获取简易教程。

- [编译构建](docs/quick_start.md)：介绍搭建环境、源码编译和UT验证等操作。
- [样例说明](examples/README.md)：提供asc-comm API样例说明。

## 📖相关资源

- **文档**

  | 文档 | 说明 |
  |---------|--------|
  | [文档入口](./docs/README.md) | asc-comm文档总入口。 |
  | [API参考](./docs/api/README.md) | asc-comm当前公开接口列表。 |
  | [Hcomm使用说明](./docs/guide/hcomm_usage.md) | Hcomm点对点通信接口的基本使用流程。 |
  | [构建与测试](./docs/guide/build_and_test.md) | CANN环境、构建脚本和UT构建说明。 |
  | [三方依赖与兼容性](./docs/guide/dependencies.md) | 本仓直接依赖、安装配置和外部集成依赖边界。 |

- **贡献指南**

  | 文档 | 说明 |
  |---------|--------|
  | [CANN社区贡献指南](https://gitcode.com/cann/community) | CANN社区Issue、PR等通用处理流程。 |
  | [asc-comm贡献指南](./CONTRIBUTING.md) | 本仓Issue、开发、检查和PR提交流程。 |

- **其他**

  | 文档 | 说明 |
  |---------|--------|
  | [更新日志](./CHANGELOG.md) | 版本变更记录。 |
  | [三方开源软件清单](./Third_Party_Open_Source_Software_List.yaml) | 本仓三方开源软件清单。 |
  | [三方开源软件声明](./Third_Party_Open_Source_Software_Notice) | 本仓三方开源软件声明。 |

## 📝相关信息

- [贡献指南](CONTRIBUTING.md)
- [安全声明](SECURITY.md)
- [许可证](LICENSE)
