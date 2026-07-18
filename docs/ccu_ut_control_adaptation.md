# CCU UT Control 适配记录

## 1. 目标

在不修改 `src/` 产品源码的前提下，补齐 `reps/control` UT 的显式数据面源码闭包和测试侧资源桩，使函数块、函数调用、跳转和跳转标签测试能够独立构建运行。

## 2. 依赖分析

control UT 覆盖 `CcuRepFuncBlock`、`CcuRepFuncCall`、`CcuRepJump` 和 `CcuRepJumpLabel`。函数块实现会链接 translator；translator 又会实例化 loop/loadarg 相关路径，并引用以下控制面静态接口：

```cpp
hcomm::CcuDevMgrImp::GetXnBaseAddr(uint32_t, uint8_t, uint64_t &)
```

现有测试未执行设备资源获取或 translator 的运行路径，但这些符号仍是被直接编入目标的产品对象的链接依赖。

## 3. 测试侧适配

### 3.1 显式产品源码闭包

`tests/ccu/hcomm/ccu_representation/reps/control/CMakeLists.txt` 补入：

- `ccu_rep_loadarg.cc`。
- `ccu_rep_loop.cc`。
- `ccu_rep_loopblock.cc`。
- `ccu_rep_loopcall.cc`。
- `ccu_rep_setloop.cc`。
- `ccu_assist.cc`。

并加入 `${CCU_IMPL_INCLUDE_DIRS}`，用于解析原始 context、channel 和 device 相关 hcomm 头文件。

### 3.2 测试桩

新增：

```text
tests/ccu/stub/ccu_dev_mgr_stub.cc
```

该文件仅实现 `CcuDevMgrImp::GetXnBaseAddr()`，返回成功和零基地址，不创建设备、transport 或其他控制面资源。

`exception_stub.cc` 增加最小 `memcpy_s()`，满足 translator 对安全拷贝接口的链接需求。

所有变更限制在 `tests/` 与 `docs/`。

## 4. 验证

执行：

```bash
source "$ASCEND_HOME_PATH/set_env.sh"
cmake -S tests/ut -B build/ut-hcomm \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DCANN_3RD_LIB_PATH="$PWD/third_party"
cmake --build build/ut-hcomm --target hccl_utest_comms_ccu_rep_control -j8
build/ut-hcomm/ccu_hcomm/ccu_representation/reps/control/hccl_utest_comms_ccu_rep_control
```

结果：`hccl_utest_comms_ccu_rep_control` 的 32 项测试全部通过。

## 5. 覆盖范围

当前用例验证函数块/调用和跳转 REP 的构造、描述、参数定义、引用及部分翻译行为。

`CcuDevMgrImp::GetXnBaseAddr()` 是链接桩，当前测试不验证真实设备资源基地址，也不应借此测试控制面设备管理行为。
