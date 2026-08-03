# NCCL User Guide 中文笔记

> 来源：NVIDIA Collective Communication Library (NCCL) User Guide，**NCCL 2.30.7**
> https://docs.nvidia.com/deeplearning/nccl/archives/nccl_2307/user-guide/docs/index.html
>
> 注意：URL 里的 `nccl_2307` 指的是 **2.30.7**，不是 2.3.07。

本目录每个文件对应原文一个章节，是中文技术笔记：完整覆盖该章的概念、规则、约束和 API 签名，代码示例保留原样，但按笔记方式组织表述，不是逐句直译。原文为准。

## 文件索引

### 正文
| 文件 | 原文章节 |
| --- | --- |
| `01-overview.md` | Overview of NCCL |
| `02-setup.md` | Setup |
| `03-using-nccl.md` | Using NCCL |
| `04-creating-a-communicator.md` | Creating a Communicator |
| `05-collective-operations.md` | Collective Operations |
| `06-data-pointers.md` | Data Pointers |
| `07-cuda-stream-semantics.md` | CUDA Stream Semantics |
| `08-group-calls.md` | Group Calls |
| `09-point-to-point.md` | Point-to-point communication |
| `10-thread-safety.md` | Thread Safety |
| `11-inplace-operations.md` | In-place Operations |
| `12-cuda-graphs.md` | Using NCCL with CUDA Graphs |
| `13-user-buffer-registration.md` | User Buffer Registration |
| `14-device-initiated-communication.md` | Device-Initiated Communication |

### API 参考
| 文件 | 原文章节 |
| --- | --- |
| `15-api-overview.md` | NCCL API |
| `16-api-communicators.md` | Communicator Creation and Management Functions |
| `17-api-collectives.md` | Collective Communication Functions |
| `18-api-group.md` | Group Calls (API) |
| `19-api-p2p.md` | Point To Point Communication Functions |
| `20-api-types.md` | Types |
| `21-api-user-defined-ops.md` | User Defined Reduction Operators |
| `22-api-flags.md` | NCCL API Supported Flags |
| `23-api-device.md` | Device API |
| `24-api-device-setup.md` | Device API – Host-Side Setup |
| `25-api-device-memory.md` | Device API – Memory and LSA |
| `26-api-device-gin.md` | Device API – GIN |
| `27-api-device-reducecopy.md` | Device API – Remote Reduce and Copy |
| `28-api-param.md` | NCCL Parameter API |

### 其他
| 文件 | 原文章节 |
| --- | --- |
| `29-language-bindings.md` | Language Bindings |
| `30-migrating-from-nccl1.md` | Migrating from NCCL 1 to NCCL 2 |
| `31-examples.md` | Examples |
| `32-nccl-and-mpi.md` | NCCL and MPI |
| `33-environment-variables.md` | Environment Variables |
| `34-troubleshooting.md` | Troubleshooting |
| `35-troubleshooting-gpu.md` | GPU troubleshooting |
| `36-troubleshooting-networking.md` | Networking Troubleshooting |
| `37-troubleshooting-runtime-mpi.md` | Runtime and MPI issues |
| `38-troubleshooting-performance.md` | Performance and tuning |
| `39-troubleshooting-logging.md` | Logging |
| `40-troubleshooting-ras.md` | RAS |

Python 绑定（NCCL4Py，原文 16 章）按约定跳过。
