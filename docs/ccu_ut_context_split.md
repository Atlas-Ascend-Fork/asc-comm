# CCU UT Context 测试适配记录

## 1. 目标

在不修改 `src/` 下 `CcuRepContext` 产品源码的前提下，适配 `reps/common` 和 context UT 的编译、链接与运行依赖。

## 2. 产品源码约束

保持以下产品源码不变：

```cpp
src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/context/ccu_rep_context.cc
src/ccu/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/context/ccu_rep_context_v1.h
```

其中的 channel profiling 重载保持原有实现：

```cpp
AddProfiling(ChannelHandle, const std::string &, uint32_t, uint32_t)
AddProfiling(const ChannelHandle *, uint32_t)
AddProfiling(const ChannelHandle *, uint32_t, HcommDataType, HcommDataType, HcommReduceOp)
```

不新增或拆分任何产品实现文件。

## 3. UT 改造

新增 context UT 目标：

```text
tests/ccu/hcomm/ccu_representation/context/CMakeLists.txt
tests/ccu/hcomm/ccu_representation/context/main.cc
```

并在 `tests/ccu/hcomm/CMakeLists.txt` 注册：

```cmake
add_subdirectory(ccu_representation/context)
```

该目标显式编译 context 所需的源码：

- context。
- common REP。
- arithmetic REP。
- loopgroup REP。
- datatype。
- microcode 和 assist。

测试目标额外加入 `${CCU_IMPL_INCLUDE_DIRS}`，使原始 context 源码依赖的 hcomm channel 头文件可被解析；运行时通过 `asccomm_ccu_llt_stub` 提供的桩满足 channel 相关符号。

同时修正既有 context UT 与当前公开接口及实现语义不一致的断言：

- `AddSqeProfiling()` 改为传入 kernel 名称。
- SQE profiling 的 dieId 断言为当前实现使用的默认值 `0`。
- loopgroup REP 收集断言改为检查 `allLgProfilingReps`，该容器由 `CollectProfilingReps()` 直接维护。
- 为测试直接使用的 `CcuRepNop` 和 `CcuRepAssign` 添加显式头文件。

## 4. 测试桩补充

`tests/ccu/stub/exception_stub.cc` 新增最小 `memset_s()` 实现。

该桩支持以下纯 context 路径：

- `Hccl::CcuProfilingInfo` 的初始化。
- 无 channel 的 `AddProfiling(const std::string &, uint32_t)`。

实现仅在目标地址为空或写入长度超过目标缓冲区长度时返回失败；其他情况调用 `memset()` 并返回成功。

## 5. 验证结果

执行：

```bash
cmake --build build/ut-hcomm \
  --target hccl_utest_comms_ccu_rep_common hccl_utest_comms_ccu_rep_context -j8
```

运行结果：

| 目标 | 结果 |
| --- | --- |
| `hccl_utest_comms_ccu_rep_common` | 28 项通过 |
| `hccl_utest_comms_ccu_rep_context` | 25 项通过 |

同时执行：

```bash
git diff --check
```

结果无格式错误。

## 6. 后续工作

下一步处理 `reps/data`：

1. 将 `HcommChannelGet` 从通用异常桩中拆出。
2. 提供可配置的 `CcuUrmaChannel` fake，覆盖 local 和 remote 数据路径。
3. 解决 data UT 自定义 channel 查询实现与公共 stub 的符号冲突。
4. 所有改动限定在 `tests/` 和 `docs/`，不修改产品源码。
