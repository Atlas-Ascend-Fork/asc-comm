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
