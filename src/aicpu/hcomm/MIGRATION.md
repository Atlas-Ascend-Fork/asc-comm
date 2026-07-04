# A5 AICPU 数据面迁移说明

本目录（`src/aicpu/hcomm` 与 `include/aicpu/hcomm`）是从 hcomm 仓迁出的 **A5（Ascend 950）AICPU 数据面**模块。

## 迁移内容

| 项 | 数量 | 说明 |
| --- | ---: | --- |
| 头文件 | 284 | `include/aicpu/hcomm/`，保留原 hcomm 目录层级 |
| 源文件 | 115 | `src/aicpu/hcomm/` |
| compat 薄头 | 4 | `src/aicpu/common/hccl_compat/`（对接控制面） |

顶层分布（头）：

- `ascend950/` (181)：A5 本体——stream/aicpu（RtsqA5/StreamLite/sqe_build_a5）、transport/aicpu（p2p/roce/ub/mem lite）、connection/buffer/notify、interface、common（异常/类型/工具）。
- `ascend910/` (68)：A5 真实依赖的公共头——`pub_inc/`（`LocalNotify`、`Stream`、`HcclBuf`、`DeviceMem`、dispatcher/notify/transport 类型、`new/hccl_primitive*`）、`platform/`（adapter_hal/rts/hccp、externalinput、dispatcher_pub）、5 个带实现头对应的 `.cc`（log/local_notify/device_capacity/dispatcher/hccl_ip_address）。
- `base_comm/`、`common/`、`communicator/`、`dfx/`、`hccl/`、`primitives/`、`resources/`：适配层、Thread 抽象、公共类型契约。

## 目录映射规则

- 头文件：`<原路径>` 去掉前缀（`src/legacy/`｜`src/base_comm/`｜`src/`｜`include/`｜`pkg_inc/`）→ `include/aicpu/hcomm/<剩余层级>`。
- 源文件：同理 → `src/aicpu/hcomm/<剩余层级>`。
- 保留原层级（如 `ascend950/...`），使路径限定 include 与少量相对 include 无需改写，靠 `-I` 根解析。

## 构建

`src/aicpu/hcomm/CMakeLists.txt`：

- 递归 GLOB `include/aicpu/hcomm` 全部子目录作为 `-I` 根（复刻原 hcomm 对 ascend950 的 GLOB 方式）。
- GLOB `src/aicpu/hcomm/*.cc` 为源；一并纳入 `src/aicpu/common/hccl_compat`（若有）。
- 复用 `tests/ut` 的 CANN 包探测，提供 acl/runtime/hal/profiling 头根。
- 产出静态库 `ccl_kernel`，定义 `CCL_KERNEL_AICPU`（A5 eager/batch 判定等依赖此宏）。

## 外部第三方依赖（`third_party/`）

A5 数据面除了 CANN/驱动 SDK（acl/runtime/hal/profiling，由构建期 `ASCEND_CANN_PACKAGE_PATH` 提供）外，还依赖若干第三方库。这些**不随迁移搬入源码树**，而由 asc-comm 仓根的 `third_party/` 目录提供（该目录为构建期本地物料，未纳入 git 跟踪）。`third_party/pkg/` 存放源码 tarball（含 rdma-core patch），`third_party/lib_cache/` 存放已解压/预编译缓存，`third_party/json/` 为 header-only 直接展开。

| third_party 子项 | 版本 | A5 数据面用途 | 引用点（迁移代码） |
| --- | --- | --- | --- |
| `json/`、`pkg/json-3.11.3.tar.gz` | nlohmann json 3.11.3 | 配置 / rank table 解析 | `nlohmann/json.hpp`、`json_parser.h` |
| `pkg/device/rdma-core-42.7.tar.gz`（+ patch）、`lib_cache/device/rdma_core_{src,build}` | rdma-core 42.7 | RoCE/RDMA 传输与连接（`verbs.h`/`ibv_*`） | `roce_transport_lite_impl.cc`、`rma_conn_lite.cc`、`rdma_conn_lite_v2.cc`、`rdma_vendor_1825_ops.*` |
| `pkg/device/openssl-openssl-3.0.9.tar.gz`、`lib_cache/device/openssl` | openssl 3.0.9 | 建链 / 安全 | 连接层（openssl `buffer.h`/`types.h`） |
| `pkg/device/cann-hcomm-utils_9.1.0`、`lib_cache/device/hcomm_utils` | hcomm-utils 9.1.0 | 通用工具库 | 工具依赖 |

> 说明：这些正是自洽性验证中列为「外部 SDK、由构建期提供」的一批依赖。构建 A5 数据面 Kernel（`libccl_kernel.so`）时，CMake 需能通过 `third_party/lib_cache` 与 CANN 包找到上述库的头与链接产物。

## 编译边界与 compat 兼容层

**控制面（comm 生命周期管理、独立算子通信域、DFX/profiling）保留在 hcomm 仓，不随数据面迁移。** 数据面适配层仅在 `HcommAcquireComm`/`HcommReleaseComm`/`HcommProfilingReport*`/`HcclDfxRegOpInfoByCommId` 等**非数据面原语**路径上引用少量控制面接口，通过 `src/aicpu/common/hccl_compat/` 的薄头对接，实现由 hcomm 控制面在链接期提供。

迁移中的处理：

1. **移除误迁的管理面实现**（0 处数据面调用，属控制面，保留在 hcomm）：`coll_comm_aicpu.cc`、`coll_comm_aicpu_mgr.cc`、`aicpu_indop_process.cc`、`queue_notify_manager.*`。
2. **新增 compat 薄头**（`src/aicpu/common/hccl_compat/`，共 4 个）：
   - `communicator/device/aicpu_indop_process.h`：仅声明适配层实际调用的 5 个接口（`AicpuDfxOpInfoInit`/`AicpuGetCommMgrbyGroup`/`AicpuReleaseCommMgrbyGroup`/`ProfilingReportDeviceOp`/`UpdateTask`），`CollCommAicpuMgr` 前向声明（数据面仅作指针空判，不访问成员），剥离非 A5 框架依赖。
   - `communicator/device/coll_comm_aicpu_mgr.h`：`CollCommAicpuMgr` 前向声明（适配层第 20 行 include 用；仅作不透明指针）。
   - `device/framework/aicpu_hccl_process.h`：`AicpuHcclProcess`（`AicpuGetCommbyGroup`/`AicpuReleaseCommbyGroup`）与 `hccl::HcclCommAicpu`（含 `SetDispatcherCtxOnThread()`，因适配层非 A5 分支有成员调用，故声明该方法）。供适配层控制面函数中 `if (deviceType != DEV_TYPE_950)` 的非 A5 分支编译通过——该分支在 A5 运行期不执行。
   - `algorithm/pub_inc/common.h`：复制原 `common.h`（7 个算法类型：`AlgType`/`RankInfo`/`TopoType`/算法枚举，由 DFX/operator 头引用），注释掉 3 个非 A5 框架 include（`alg_cmd_type.h`/`template_v1_utils.h`/`comm_utils.h`，其符号并未被这 7 个类型使用）。

**自洽性验证**：对全部 115 个 `.cc` 做预处理，迁移树内部 include **零缺失**，仅余外部 SDK（`acl/*`、`ascend_hal.h`、`securec.h`、`dlog_pub.h`、`mmpa`、`prof_common.h`、`rt_external.h`、`nlohmann/json.hpp` 等，由 CANN/驱动在编译期提供）。

## 剥离项（非 A5 冗余，已刻意排除）

原 hcomm 的 `ascend910/algorithm/*`、`ascend910/framework/*` 共 62 个头（非 A5 的算法与框架层）在迁移时剥离，通过上述 compat 边界隔离，避免把 A2/A3 旧路径带入。
