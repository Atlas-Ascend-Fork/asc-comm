# CCU 直调 Host Launch ERROR 日志增强方案

## 1. 背景

`src/ccu/ccu_host_launch.cpp` 是 `<<<>>>` CCU 直调 Host 侧实现，负责校验启动配置、注册 Kernel、生成 SQE 任务参数并调用 `rtCCULaunch`。

原实现对异常参数仅返回错误码，缺少能够定位问题的 ERROR 日志。重点关注以下场景：

- `num_blocks` 不符合单 mission 限制，例如传入 `2`；
- `phy_die_mask` 不是合法的单 Die 掩码；
- SQE 参数数量超过 13 个；
- `stream` 为空指针。

## 2. 日志方式分析

仓内主机侧代码统一使用 `ASC_CPU_LOG_ERROR`，典型用法位于 `src/aicore/hcomm/host/hcomm_host_impl.h`：

```cpp
ASC_CPU_LOG_ERROR("[ERROR] [%s] operation failed, ret[%d].", __func__, ret);
```

该宏定义在 CANN 的 `utils/log/asc_cpu_log.h` 中，底层通过 `dlog_error` 输出 CANN slog/dlog 日志。正式库代码不建议直接使用 `printf`，因为直接标准输出不一定进入 CANN 的统一日志系统。

## 3. 实现方案

### 3.1 启动配置校验

在 `ValidateLaunchCfg` 中，对两个配置字段分别记录实际值和约束：

```cpp
if (cfg->ccu_schd.num_blocks != CCU_SUPPORTED_NUM_BLOCKS) {
    ASCCOMM_CCU_LOG_ERROR(
        "[ERROR] [%s] unsupported num_blocks[%u], expected[%u].", __func__,
        cfg->ccu_schd.num_blocks, CCU_SUPPORTED_NUM_BLOCKS);
    return CCU_E_PARA;
}

if (!IsSupportedSingleDieMask(cfg->ccu_schd.phy_die_mask)) {
    ASCCOMM_CCU_LOG_ERROR(
        "[ERROR] [%s] unsupported phy_die_mask[0x%llx], expected[0x1 or 0x2].",
        __func__, static_cast<unsigned long long>(cfg->ccu_schd.phy_die_mask));
    return CCU_E_PARA;
}
```

当前限制是：

- `num_blocks` 只能为 `1`；
- `phy_die_mask` 只能为 `0x01` 或 `0x02`。

### 3.2 SQE 参数数量校验

在 `GetSingleTaskParam` 中保留原有返回码 `CCU_E_NOT_SUPPORT`，并记录参数数量：

```cpp
if (argNum > hcomm::CCU_SQE_ARGS_LEN) {
    ASCCOMM_CCU_LOG_ERROR(
        "[ERROR] [%s] SQE argument count[%u] exceeds maximum[%u].", __func__,
        argNum, hcomm::CCU_SQE_ARGS_LEN);
    return CCU_E_NOT_SUPPORT;
}
```

`hcomm::CCU_SQE_ARGS_LEN` 当前为 `13`。

### 3.3 Stream 空指针校验

在 `LaunchSingleCcuTask` 的既有空指针返回点前增加日志：

```cpp
if (stream == nullptr) {
    ASCCOMM_CCU_LOG_ERROR("[ERROR] [%s] stream is nullptr.", __func__);
    return CCU_E_PTR;
}
```

### 3.4 弱日志后端保护

单元测试或最小运行环境可能没有提供弱符号 `DlogRecord`。直接调用 `ASC_CPU_LOG_ERROR` 会在该环境中调用空函数指针，因此增加轻量保护：

```cpp
#define ASCCOMM_CCU_LOG_ERROR(...)     \
    do {                               \
        if (DlogRecord != nullptr) {   \
            ASC_CPU_LOG_ERROR(__VA_ARGS__); \
        }                              \
    } while (0)
```

生产环境加载 CANN 日志后端时正常输出 ERROR；无日志后端时跳过日志，不改变原有返回码和控制流。

### 3.5 最终库链接

在 `src/ccu/CMakeLists.txt` 的 `asccomm_ccu` 共享库链接列表中增加：

```cmake
$<$<TARGET_EXISTS:unified_dlog>:unified_dlog>
```

这样在上层 CANN 构建系统提供 `unified_dlog` Target 时，日志依赖会链接到最终共享库，而不仅停留在对象目标上。

## 4. 参数精度和测试语义说明

### 4.1 `num_blocks`

公开接口中的 `num_blocks` 类型是 `uint32_t`。如果调用侧使用 `int64` 范围的值，值在进入本实现前已经发生窄化，日志只能打印窄化后的 `uint32_t` 实际值，无法恢复调用侧的原始 `int64` 值。

### 4.2 `phy_die_mask = 2`

`phy_die_mask = 2` 即 `0x02`，对应合法的 Die1 掩码。当前实现明确支持 `0x01` 和 `0x02`，因此该值不会触发非法参数 ERROR。只有其他掩码值（例如 `0`、`0x03` 或超出支持范围的值）才会记录配置错误。

## 5. 修改文件

- `src/ccu/ccu_host_launch.cpp`
  - 引入 `utils/log/asc_cpu_log.h`；
  - 增加 `num_blocks`、`phy_die_mask`、SQE 参数数量和空 stream 的 ERROR 日志；
  - 增加无 `DlogRecord` 后端时的保护。
- `src/ccu/CMakeLists.txt`
  - 为最终 `asccomm_ccu` 共享库增加条件 `unified_dlog` 链接。

## 6. 验证结果

使用 CANN 9.2.0 环境完成以下验证：

```text
CCU 共享库编译通过
CCU 单元测试编译通过
TestCcuHostLaunch.*：9 个测试全部通过
git diff --check：通过
```

单元测试在没有日志后端的环境下也能保持原有返回码语义；在实际 CANN 运行环境中，日志由统一 dlog/slog 后端接收。
