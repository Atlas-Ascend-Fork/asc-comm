# CCU AOT用户文档

本文档面向使用CCU AOT SDK编写、编译和运行CCU kernel的开发者，内容只保留当前SDK
的公开接口和可执行用法。

## 文档目录

| 文档 | 内容 |
| --- | --- |
| [API参考](api/README.md) | Host控制面、CCU intrinsic和编译接口总入口 |
| [Host控制面API](api/host/README.md) | context、instance、buffer、launch、同步和错误处理 |
| [CCU数据面API](api/README.md#数据面接口分类) | 参数、资源、搬运、同步和流程控制接口 |
| [编译接口](api/compiler.md) | `ccuc`命令、输入文件和产物 |
| [快速开始](guide/quick_start.md) | 从SDK到第一个可运行kernel |
| [编译与运行](guide/compile_and_run.md) | Host/kernel分文件编译、链接和运行 |
| [集合通信样例](guide/collective_example.md) | 以AllGather为例的控制面与数据面调用 |

## 适用范围

- Host控制面使用`<ccu_runtime.h>`。
- CCU数据面使用`<ccu_intrinsics.h>`和`ccuc`编译器。
- 通信域、通信channel、通信buffer和memory token由部署环境提供的HCOMM public
  接口管理。
- 本文档不描述完整编程模型、内部实现或私有实现细节。

## 版本约定

文档以随CCU AOT SDK发布的`ccu_runtime.h`、`ccu_intrinsics.h`和public examples为准。
若头文件与本文档不一致，应以当前SDK头文件和编译器诊断为准。
