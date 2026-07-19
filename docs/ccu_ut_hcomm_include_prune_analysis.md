# CCU UT hcomm Include 精简分析

## 1. 范围

本文分析 `tests/ut/CMakeLists.txt` 中 `UT_COMMON_INCLUDE_LIST` 的 hcomm 路径，重点覆盖第 243 至 297 行。

当前已启用 CCU UT 包括 sync、control、common、data、arithmetic、loop、context、interface 和 microcode。

## 2. 当前依赖特征

CCU UT 仍有两类 hcomm 依赖：

- 必需头文件：legacy 异常、字符串、类型、日志、RTS/runtime、公开接口及 securec。
- 测试桩类型：`CcuUrmaChannel`、`CcuDevMgrImp` 等 hcomm 控制面类型仅由 tests 中的桩实现使用。

此外，`CCU_IMPL_INCLUDE_DIRS` 会递归收集 hcomm `src/include/pkg_inc` 下含头文件的目录，供 stub、common、context、data、control 和 interface 目标使用。microcode、arithmetic、loop 不使用该递归列表，因此其直接依赖路径必须继续保留在 `UT_COMMON_INCLUDE_LIST` 中。

## 3. 目录分类

第 243 至 297 行共 55 个 hcomm include 目录：

| 分类 | 数量 | 处理 |
| --- | --- | --- |
| 必需保留 | 10 | 当前不修改 |
| 第一批删除 | 38 | 本次删除并验证 |
| 待定向验证 | 6 | 本次保留 |

### 3.1 必需保留

```text
src/legacy/ascend910/pub_inc
src/legacy/ascend950/common/exception
src/legacy/ascend950/common/utils
src/legacy/ascend950/common/types
src/base_comm/resources/southbound_adpt
include
include/hccl
pkg_inc/legacy
test/stub/depends/pkg_inc/runtime
test/stub/depends/include
```

关键依赖如下：

- `log.h`：microcode v2 使用，且 ascend910 路径必须早于 ascend950 common。
- `ccu_api_exception.h`、`exception_util.h`、`string_util.h`、`data_type.h`、`reduce_op.h`：REP 和 microcode 直接或传递使用。
- `hcomm_adapter_rts.h` 与 `rt_external.h`：`ccu_assist.cc` 和 RTS 测试桩使用。
- `hcomm_primitives.h`、`hccl_types.h`、`hccl/base.h`、`securec.h`：representation/context 和 legacy 日志链使用。

### 3.2 第一批删除

本次删除以下 38 个目录：

```text
src/legacy/ascend910/pub_inc/inner
src/legacy/ascend910/pub_inc/new
src/legacy/ascend910/platform/common
src/legacy/ascend910/common
src/legacy/ascend910/framework/common/src
src/legacy/ascend910/framework/inc
src/legacy/ascend910/framework/hcom
src/legacy/ascend910
src/legacy/ascend950/unified_platform
src/base_comm
src/base_comm/common
src/base_comm/resources
src/base_comm/resources/ccu
src/base_comm/resources/ccu/ccu_device
src/base_comm/resources/ccu/ccu_device/ccu_comp
src/base_comm/resources/ccu/ccu_device/ccu_comp/ccu_channel
src/base_comm/resources/ccu/ccu_transport
src/base_comm/resources/ccu/ccu_kernel
src/base_comm/resources/ccu/ccu_instance
src/base_comm/resources/endpoint_pairs/channels
src/base_comm/resources/endpoint_pairs/channels/ccu
src/base_comm/resources/hccp/inc
src/base_comm/resources/hccp/inc/network
src/base_comm/primitives/api_c_adpt
src
pkg_inc/legacy/hccl
src/base_comm/resources/ccu/ccu_representation
src/base_comm/resources/ccu/ccu_representation/context
src/base_comm/resources/ccu/ccu_representation/interface
src/base_comm/resources/ccu/ccu_representation/reps/arithmetic
src/base_comm/resources/ccu/ccu_representation/reps/common
src/base_comm/resources/ccu/ccu_representation/reps/control
src/base_comm/resources/ccu/ccu_representation/reps/data
src/base_comm/resources/ccu/ccu_representation/reps/loop
src/base_comm/resources/ccu/ccu_representation/reps/sync
src/base_comm/resources/ccu/ccu_representation/reps/translator
src/base_comm/resources/ccu/ccu_microcode
src/base_comm/resources/ccu/pub_inc
```

这些路径要么已由 `CCU_IMPL_INCLUDE_DIRS` 递归覆盖，要么当前 UT 显式编译 asc-comm 副本并通过 asc-comm include 路径解析。删除 hcomm 旧 representation/microcode 目录还可避免同名头文件意外命中。

### 3.3 待定向验证

以下路径暂时保留：

```text
src/legacy/ascend950
src/legacy/ascend950/common
pkg_inc
pkg_inc/hccl
test/stub/depends/pkg_inc
test/stub/depends/include/mmpa
```

其中 `ascend950/common` 可能影响同名 `log.h` 搜索顺序；其余路径未定位到当前直接依赖，但可能存在 package 兼容头或外部头的间接裸名 include。

## 4. 验证标准

第一批精简后执行：

```bash
source "$ASCEND_HOME_PATH/set_env.sh"
bash build.sh -t --cov
```

通过标准：

- 9 个 CCU CTest 全部通过。
- `build/ut-hcomm/cov_report/index.html` 生成成功。
- 覆盖率信息仍包含 CCU representation 与 microcode 源码。

## 5. 第一批验证结果

初次执行默认构建与覆盖率时，已在 AICore UT 入口保持注释的状态下运行：

```bash
bash build.sh -t --cov
```

默认构建未生成 arithmetic 和 loop 可执行文件，因此其 CTest 未被实际执行。独立构建这两个已注册 target 后，均因 `task_param.h` 缺失失败；该头文件来自原删除清单中的 `src/legacy/ascend950/unified_platform/pub_inc`。

该路径已恢复至保留集合。后续验证必须显式构建 arithmetic 和 loop，再运行 9 项 CCU CTest。默认 `build.sh -t --cov` 的成功退出不能单独作为 9 项 CTest 均已验证的依据。

恢复后执行：

```bash
cmake --build build/ut-hcomm --target \
    hccl_utest_comms_ccu_rep_arithmetic hccl_utest_comms_ccu_rep_loop -j 64
ctest --test-dir build/ut-hcomm/ccu_hcomm --output-on-failure
cmake --build build/ut-hcomm --target collect_coverage_data -j 64
```

结果：

- arithmetic 和 loop target 均构建成功。
- CCU CTest 9/9 通过。
- `build/ut-hcomm/cov_report/index.html` 生成成功。
- 覆盖率信息包含 `task_param.h`，证明该保留路径被 arithmetic 和 loop 目标实际使用。
- 本轮覆盖率为：行 51.8%，函数 63.3%。

## 6. 第二批候选分析

第一批后的剩余待定路径及 `.d` 编译依赖结论如下：

| 路径 | 证据 | 结论 |
| --- | --- | --- |
| `src/legacy/ascend950` | `ccu_device_pub.h` 通过 `unified_platform/pub_inc/ccu/ccu_dev_mgr.h` 前缀包含 | 保留 |
| `src/legacy/ascend950/common` | 命中 `ip_address.h`、`binary_stream.h`、`log.h` 等 | 保留 |
| `pkg_inc` | 未直接命中根目录文件，但可能用于 `hccl/`、`hcomm/` 前缀 include | 待单独验证 |
| `pkg_inc/hccl` | 命中 `hccl_diag.h` | 保留 |
| `test/stub/depends/pkg_inc` | 命中 `profiling/aprof_pub.h` | 保留 |
| `test/stub/depends/include/mmpa` | 未命中 `mmpa_api.h` 或其子目录头文件 | 删除并验证 |

本批先删除 `src/legacy/ascend950` 与 `test/stub/depends/include/mmpa`。前者验证失败：`ccu_device_pub.h` 包含 `unified_platform/pub_inc/ccu/ccu_dev_mgr.h`，必须通过 ascend950 根目录解析，现已恢复。后者尚未命中编译依赖，待在前者恢复后继续验证；其余路径不因未完成的前缀 include 分析而提前删除。

恢复 ascend950 根目录后重新执行 `bash build.sh -t`，9 个 CCU target 全部构建并通过 CTest。由此确认 `test/stub/depends/include/mmpa` 可删除，第二批净删除 1 个目录。

## 7. 第三批候选分析

`pkg_inc` 根目录未出现在当前 CCU target 的 `.d` 编译依赖中；其下 `hcomm/` 目录也未被当前 CCU 源码或测试以 `hcomm/` 前缀引用。已启用 CCU 目标实际需要的 package 头文件由下列更精确路径提供：

- `pkg_inc/legacy`：`hccl/base.h`、`dtype_common.h`、`hcom.h`、`workflow.h`。
- `pkg_inc/hccl`：`hccl_diag.h`。

当前 AICore UT 入口保持注释，`pkg_inc` 根目录只可能用于该未启用路径。本批从 CCU UT include 列表删除 `pkg_inc`，并以 9 项 CCU target 构建和 CTest 验证。

验证命令：

```bash
bash build.sh -t
```

结果：9 个 CCU target 均构建成功，CTest 9/9 通过。因此，在当前仅启用 CCU UT 的配置下，`pkg_inc` 根目录可删除。

## 8. 当前结论

`UT_COMMON_INCLUDE_LIST` 的 hcomm 直接 include 路径已由 55 项收敛为 15 项：

- 已删除并验证：40 项，其中第一批 38 项、第二批 `test/stub/depends/include/mmpa`、第三批 `pkg_inc`。
- 必须保留：`src/legacy/ascend950`、`src/legacy/ascend950/common`、`include`、`include/hccl`、`pkg_inc/legacy`、`pkg_inc/hccl`、测试桩 runtime/pkg/include 路径，以及前述异常、类型和 southbound 路径。

若重新启用 AICore UT，应单独验证其 `hcomm/hcomm.h` 前缀 include 是否仍需要 `pkg_inc` 根目录；该结论不应由当前 CCU 验证外推。
