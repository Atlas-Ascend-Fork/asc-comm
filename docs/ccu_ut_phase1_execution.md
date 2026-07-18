# CCU UT 清理与数据面适配第一阶段执行记录

## 1. 阶段目标

本阶段将 CCU UT 从 hcomm 控制面测试中拆出，保留可继续演进的 CCU 数据面测试入口，并让已完成迁移的数据面目标不再依赖历史的 `asccomm_ccu_impl` 聚合对象库。

边界如下：

- 删除 hcomm 独有的设备资源、transport、连接和旧 API 控制面 UT。
- 保留 CCU 数据面 REP、microcode、kernel 和 instance 相关测试源码。
- 保留 `tests/ccu/hcomm/ccu_kernel_impl/`，其中的 demo kernel 将用于后续 API/kernel UT。
- 不恢复全量 `asccomm_ccu_impl`，避免将 hcomm 控制面依赖重新编入每一个数据面测试目标。

## 2. 已完成修改

### 2.1 删除控制面 UT 与独占测试设施

已删除以下 hcomm 控制面测试或其独占测试设施：

```text
tests/ccu/hcomm/ut_CcuComponent_test.cc
tests/ccu/hcomm/ut_CcuConn_test.cc
tests/ccu/hcomm/ut_CcuTransport_API_test.cc
tests/ccu/hcomm/ut_HcommCcuControlApi_test.cc
tests/ccu/hcomm/ccu_device/
tests/ccu/hcomm/hccl_api_base_test.h
tests/ccu/hcomm/hccl_api_base_test.cc
tests/ccu/hcomm/main.cc
tests/ccu/hcomm/mocks/
```

保留以下目录：

```text
tests/ccu/hcomm/ccu_kernel_impl/
tests/ccu/stub/
tests/ccu/hcomm/ccu_microcode/
tests/ccu/hcomm/ccu_representation/
```

`ccu_kernel_impl/` 不是 hcomm 控制面产品代码。它提供 loop、jump、func-call、group-copy 等 demo kernel，可作为后续 asc-comm API/kernel UT 的测试输入。

### 2.2 放开并改造数据面目标

`tests/ccu/hcomm/CMakeLists.txt` 已启用以下子目标：

```cmake
add_subdirectory(ccu_representation/reps/sync)
add_subdirectory(ccu_representation/reps/control)
add_subdirectory(ccu_representation/reps/common)
add_subdirectory(ccu_representation/reps/data)
add_subdirectory(ccu_representation/reps/arithmetic)
add_subdirectory(ccu_representation/reps/loop)
add_subdirectory(ccu_representation/interface)
add_subdirectory(ccu_microcode)
```

所有上述目标均已移除下列失效引用：

```cmake
$<TARGET_OBJECTS:asccomm_ccu_impl>
add_dependencies(... asccomm_ccu_impl)
```

各目标改为在自身 CMake 中显式列出所需的 asc-comm 数据面产品 `.cc` 文件。这样可以让每个目标的源码依赖可见，并避免重新引入全量 hcomm 实现。

### 2.3 microcode 独立化

`ccu_microcode` 目标直接编译以下 asc-comm 产品源码：

```text
src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode/ccu_microcode.cc
src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode/ccu_microcode_v2_.cc
src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode/ccu_assist.cc
```

删除 `ut_ccu_assist_test.cc` 中仅调用空 `GlobalMockObject::verify()` 的 mockcpp 依赖。

为覆盖 `ccu_assist.cc` 的 RTS 抽象边界，新增：

```text
tests/ccu/stub/ccu_rts_stub.cc
```

该 fake 仅实现 `RtsUbDevQueryInfo()`：对 `QUERY_PROCESS_TOKEN` 返回零值 token 和 `HCCL_SUCCESS`；其他查询命令返回 `HCCL_E_PARA`。同时新增 `GetTokenInfo_EncodesQueriedToken` 用例，验证查询结果按 CCU token 格式编码。

### 2.4 去除 REP 对控制面资源规格头的迁移残留

`ccu_rep_v1.h` 原先包含 hcomm 的 `ccu_device/ccu_res_specs.h`。该头会继续引入 `CcuResSpecifications`、HCCP 和环境配置依赖，但 asc-comm REP 源码没有使用 `CcuResSpecifications` 类型。

已移除该 include，并将 REP 实际使用的 XN slot 宽度常量迁入数据面头：

```cpp
namespace hcomm {
constexpr uint16_t CCU_RESOURCE_XN_PER_SIZE = 8;
}
```

这保持原有数值和翻译行为，同时消除 REP 编译对 hcomm 控制面资源规格头的依赖。

## 3. 验证结果

使用以下命令重新生成构建系统：

```bash
source "$ASCEND_HOME_PATH/set_env.sh"
cmake -S tests/ut -B build/ut-hcomm \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DCANN_3RD_LIB_PATH="$PWD/third_party"
```

CMake 配置和生成成功。

已构建并执行的目标如下：

| 目标 | 结果 |
| --- | --- |
| `hccl_utest_comms_ccu_microcode` | 92 项通过 |
| `hccl_utest_comms_ccu_rep_arithmetic` | 36 项通过 |
| `hccl_utest_comms_ccu_rep_loop` | 41 项通过 |
| `hccl_utest_comms_ccu_rep_sync` | 17 项通过 |

同时执行了：

```bash
git diff --check
```

结果无格式错误。

## 4. 当前剩余问题

以下目标已能参与 CMake 配置，但还不能作为独立、纯数据面目标完成构建。这些问题属于产品源码边界或测试 fake 设计问题，不应通过恢复 `asccomm_ccu_impl` 规避。

| 目标 | 当前阻塞 | 处理方向 |
| --- | --- | --- |
| `reps/common` | `CcuRepContext` 在同一 `.cc` 中同时实现 REP context 与 `CcuUrmaChannel` profiling。 | 保持产品源码不变；在 UT 目标中引入 `${CCU_IMPL_INCLUDE_DIRS}` 并链接测试桩。 |
| `reps/data` | remote 数据路径需要可配置的 `CcuUrmaChannel` fake；测试中的 `HcommChannelGet` 与公共 stub 存在符号冲突。 | 将 channel 查询 fake 从 `exception_stub.cc` 拆出，提供真实派生的可配置 `CcuUrmaChannel` fake。 |
| `reps/control` | translator 路径仍引用 `CcuDevMgrImp::GetXnBaseAddr`。 | 将 host 资源获取与纯 REP translation 分离，或定义数据面窄资源接口。 |
| `interface` | `ccu_interface_assist.cc` 直接依赖 `CcuKernel` RTTI；且与 context stub 定义的 `AppendToContext/CreateVariable` 冲突。 | 将不依赖 kernel 的辅助逻辑拆分，隔离 kernel 适配逻辑，并统一 context fake 的所有权。 |

`context/ut_ccu_rep_context_test.cc` 已有独立 CMake 入口，目标为 `hccl_utest_comms_ccu_rep_context`。

## 5. 下一阶段顺序

1. 保持 `CcuRepContext` 产品源码不变，继续通过测试侧 include 和 stub 支持已注册的 common/context UT。
2. 建立可配置的 channel fake，适配 `reps/data` 的 local/remote 路径。
3. 隔离 translator 的 `CcuDevMgrImp` 资源获取，适配 `reps/control`。
4. 在测试侧处理 interface assist 的 kernel 依赖与 context fake 符号冲突，适配 `interface`。
5. 运行全部已启用 CCU 数据面目标。

在上述工作完成前，不应通过重新定义全量 `asccomm_ccu_impl` 来使目标“暂时可编译”，否则会重新掩盖 hcomm 控制面依赖。

## 6. Context UT 适配执行结果

本阶段不修改 `src/` 下的 context 产品源码。`CcuRepContext` 保持原有 channel profiling 实现；UT 通过测试侧 CMake 和 stub 适配其编译、链接依赖。

同时新增并注册独立 context UT：

```text
tests/ccu/hcomm/ccu_representation/context/CMakeLists.txt
tests/ccu/hcomm/ccu_representation/context/main.cc
```

该目标仍直接编译原始 `ccu_rep_context.cc`，并在测试目标中补充 `${CCU_IMPL_INCLUDE_DIRS}`，使 hcomm channel 头文件可被解析。链接时使用 `asccomm_ccu_llt_stub` 提供的测试桩。

`exception_stub.cc` 增加最小 `memset_s` 实现，满足 `CcuProfilingInfo` 初始化和无 channel 的 profiling 路径。

验证结果：

| 目标 | 结果 |
| --- | --- |
| `hccl_utest_comms_ccu_rep_common` | 28 项通过 |
| `hccl_utest_comms_ccu_rep_context` | 25 项通过 |

至此，`reps/common` 和 `context` 均可在不修改产品源码的前提下独立运行。下一步可在 `tests/` 侧处理 `reps/data` 的可配置 channel fake。

## 7. Data UT 适配执行结果

`reps/data` 的 `HcommChannelGet` 已收敛为 `tests/ccu/stub/ccu_channel_get_stub.cc` 中唯一的全局实现。data UT 注册真实类型的 `CcuUrmaChannel` 测试对象，避免测试本地伪对象与产品 `dynamic_cast` 不兼容。

`hccl_utest_comms_ccu_rep_data` 已构建并通过 33 项测试。现有 remote `Translate` 用例大多仍只校验类型，详细覆盖缺口见 `docs/ccu_ut_data_adaptation.md`。

## 8. Control UT 适配执行结果

`reps/control` 已通过显式补齐 loop、loadarg 和 microcode assist 源码闭包，并使用测试侧 `CcuDevMgrImp::GetXnBaseAddr()` 窄桩满足 translator 的链接依赖。

`hccl_utest_comms_ccu_rep_control` 已构建并通过 32 项测试。该资源桩不创建或验证设备控制面资源，详细记录见 `docs/ccu_ut_control_adaptation.md`。

## 9. Interface UT 适配执行结果

`reps/interface` 已使用测试侧 assist 静态库和最小 `CcuKernel` RTTI 桩解决 production assist 的重定义与链接依赖，并通过 32 项测试。详细记录见 `docs/ccu_ut_interface_adaptation.md`。
