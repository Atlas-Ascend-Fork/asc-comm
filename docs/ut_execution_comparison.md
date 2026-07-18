# AICore 与 CCU UT 执行机制

## 1. build.sh 入口

执行：

```bash
source "$ASCEND_HOME_PATH/set_env.sh"
bash build.sh -t
```

`build.sh` 会清理 `build/`，使用 `tests/ut` 作为 CMake 源目录，并生成 `build/ut-hcomm`。

## 2. AICore UT

`tests/ut/CMakeLists.txt` 通过：

```cmake
add_subdirectory(aicore/hcomm)
```

注册 AICore UT。每个目标由 `add_hcomm_ut()` 创建，链接 tikicpu、cpudebug 和 CANN 仿真运行库。

`run_llt_test()` 为 AICore 目标设置 `POST_BUILD` 命令：目标链接完成后立即运行 Google Test 二进制，并按产品类型传入预定义 `--gtest_filter`。因此 AICore UT 的执行由 `cmake --build` 触发，不依赖 CTest 注册。

## 3. CCU UT

`tests/ut/CMakeLists.txt` 通过：

```cmake
add_subdirectory(../ccu/hcomm ccu_hcomm)
```

注册 CCU host UT。每个 CCU CMake 通过 `add_test()` 将可执行文件注册为 CTest 测试。

由于 CCU 使用相对 `add_subdirectory`，CTest 文件位于：

```text
build/ut-hcomm/ccu_hcomm/CTestTestfile.cmake
```

正确的执行命令为：

```bash
ctest --test-dir build/ut-hcomm/ccu_hcomm --output-on-failure
```

顶层 `build/ut-hcomm` 不包含 CCU 的 CTest 注册记录，不能用于发现 CCU 测试。

## 4. build.sh 当前行为

`build.sh -t` 的执行顺序为：

1. 配置 `tests/ut`。
2. 构建全部 UT 目标，触发 AICore UT 的 `POST_BUILD` 执行。
3. 对 `build/ut-hcomm/ccu_hcomm` 执行 CTest，运行已注册的 CCU UT。

`build.sh -t --cov` 在步骤 3 后调用 `collect_coverage_data`。该 target 不再是 `ALL` target，避免在 CCU 测试尚未执行时生成覆盖率报告。该步骤只收集报告；此前构建过的测试目标不会重复编译。

`collect_coverage_data` 在 `tests/ut/CMakeLists.txt` 顶层、`ENABLE_GCOV=ON` 时创建，不再依赖 AICore 的 `run_llt_test()` 调用。因此即使注释 `add_subdirectory(aicore/hcomm)`，仅 CCU UT 仍可通过 `build.sh -t --cov` 生成覆盖率报告。

## 5. 差异汇总

| 项目 | AICore UT | CCU UT |
| --- | --- | --- |
| 运行模型 | tikicpu/cpudebug 仿真 | host Google Test + 测试桩 |
| 测试触发点 | CMake `POST_BUILD` | CTest `add_test()` |
| build.sh 执行方式 | `cmake --build` 自动执行 | 构建后显式调用 CTest |
| 结果目录 | 由 `run_llt_test()` 的工作目录决定 | `build/ut-hcomm/ccu_hcomm` |
