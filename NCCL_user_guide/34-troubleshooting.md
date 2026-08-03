# 34 · Troubleshooting 总览

> 原文：Troubleshooting

**在改动 NCCL 设置之前，先用下面这几页把问题范围缩小。** 这是本章的核心建议——不要一上手就调环境变量。

| 页面 | 覆盖内容 | 本目录文件 |
| --- | --- | --- |
| GPU troubleshooting | GPU 到 GPU、GPU 到 NIC、ACS、拓扑、多节点 NVLink 问题 | `35` |
| Networking Troubleshooting | 接口选择、底层 fabric 检查、延迟与带宽测试、InfiniBand 或 RoCE 诊断 | `36` |
| Runtime and MPI issues | 基本错误处理、共享内存与运行时问题、MPI 启动验证 | `37` |
| Performance and tuning | 基线性能分诊，以及系统检查看起来健康之后可以尝试的 NCCL 调优旋钮 | `38` |
| Logging | NCCL 日志级别、子系统过滤、输出文件、时间戳 | `39` |
| RAS | NCCL 内置的 RAS 子系统，用于诊断 hang 和 crash | `40` |

排查顺序建议：先按 `35`–`37` 确认硬件、网络、运行环境本身没问题，再进 `38` 做性能调优。遇到 hang 直接看 `40`，需要更多信息就开 `39` 的日志。
