# CCU Host Launch 设备 ID 获取解耦方案

## 1. 背景

`src/ccu/ccu_host_launch.cpp` 原先通过手工声明并调用 hcomm 内部的
`HcclGetThreadDeviceId` 获取当前线程的逻辑设备 ID。

该接口在 Ascend 950 的实现位于 hcomm 的
`src/legacy/ascend950/framework/entrance/op_base/op_base_v2.cc`，实现本身是文件内
`static` 函数，并依赖 hcomm 的线程局部变量、HCCL 错误码和内部设备数量常量。因此，
asc-comm 直接依赖该符号存在以下问题：

- Ascend 950 实现不是可供外部链接的公共符号，外部声明可能绑定到其他版本实现；
- asc-comm 的 Host launch 逻辑依赖 hcomm 内部实现细节；
- 设备查询失败后返回值可能被转换为无符号类型，错误不能及时终止；
- 原有 Kernel Handle 缓存只按 `binary_cache_tag` 索引，跨设备时可能复用错误的 Handle。

本方案只解除对 `HcclGetThreadDeviceId` 这一具体接口的依赖。CCU Kernel 注册、Kernel
Manager 和任务参数生成等其他 hcomm 依赖保持不变。

## 2. 设计目标

1. 使用公开的 ACL Runtime API 获取当前线程设备 ID。
2. 不在 asc-comm 中定义同名的 `HcclGetThreadDeviceId` 符号，避免符号冲突。
3. 让设备 ID 在 Host launch 流程中显式传递。
4. 设备查询失败时在访问 hcomm Kernel Manager 前返回明确错误。
5. 避免复制 hcomm 的 TLS 缓存和私有错误码实现。
6. 保证不同设备之间不会错误复用 Kernel Handle。

## 3. 方案

### 3.1 私有设备查询函数

在 `src/ccu/ccu_host_launch.cpp` 的匿名命名空间中新增：

```cpp
CcuResult GetCurrentDeviceId(int32_t& deviceId)
{
    deviceId = -1;
    const aclError ret = aclrtGetDevice(&deviceId);
    if (ret != ACL_SUCCESS) {
        return CCU_E_RUNTIME;
    }
    if (deviceId < 0) {
        return CCU_E_INTERNAL;
    }
    return CCU_SUCCESS;
}
```

该函数只依赖 `acl/acl_rt.h` 和公开的 `aclrtGetDevice`，不导出新的公共 ABI。

错误映射如下：

| 场景 | 返回值 |
| --- | --- |
| `aclrtGetDevice` 调用失败 | `CCU_E_RUNTIME` |
| Runtime 返回负设备 ID | `CCU_E_INTERNAL` |
| 查询成功 | `CCU_SUCCESS` |

不复制 hcomm 中的 `thread_local g_hcclDeviceId`。每次 Host launch 重新查询当前设备，
这样可以正确处理同一线程执行 `aclrtSetDevice` 后的设备切换，也避免 asc-comm 自己的
缓存无法感知 hcomm 内部刷新逻辑。

### 3.2 显式传递设备 ID

`asccomm_ccu_host_kernel_launch` 在校验参数和检查 CCU API 可用性后调用
`GetCurrentDeviceId`，然后将设备 ID传递给：

- `KernelHandleCache::Match`；
- `GetSingleTaskParam`；
- `hcomm::CcuKernelMgr::GetInstance`。

原来的调用：

```cpp
const uint32_t devLogicId = static_cast<uint32_t>(HcclGetThreadDeviceId());
```

被删除，避免查询失败的 `-1` 被转换为无符号设备 ID。

### 3.3 缓存按设备隔离

Kernel Handle 缓存从：

```cpp
std::unordered_map<uint64_t, CcuKernelHandle>
```

调整为：

```cpp
std::unordered_map<int32_t, std::unordered_map<uint64_t, CcuKernelHandle>>
```

第一层使用逻辑设备 ID，第二层使用 `binary_cache_tag`。相同 tag 在不同设备上分别
注册和缓存，避免跨设备复用不属于当前设备的 Handle。

## 4. 构建修改

在 `src/ccu/CMakeLists.txt` 中增加 `find_library`，从 CANN 安装目录查找
`libascendcl.so`，搜索路径包括：

- `${ASCCOMM_CANN_ARCH_DIR}/lib64`；
- `${ASCCOMM_CANN_ARCH_DIR}/devlib`；
- `${ASCEND_CANN_PACKAGE_PATH}/lib64`。

若找不到 `libascendcl.so`，配置阶段直接报错；找到后将其显式链接到
`asccomm_ccu`。这样 `aclrtGetDevice` 的依赖由 asc-comm 自己声明，不依靠 hcomm 的
传递链接关系。

## 5. 单元测试

`tests/ut/ccu/test_ccu_host_launch.cpp` 移除了 `HcclGetThreadDeviceId` stub，改为提供
可控的 `aclrtGetDevice` stub，并补充以下覆盖：

- 当前设备 ID 被正确传递给 `CcuKernelMgr`；
- ACL Runtime 查询失败时返回 `CCU_E_RUNTIME`；
- 负设备 ID 时返回 `CCU_E_INTERNAL`；
- 同一线程切换设备后使用新的设备 ID；
- 相同 cache tag 在不同设备上分别注册，回到原设备时复用原设备缓存。

## 6. 修改文件

| 文件 | 修改内容 |
| --- | --- |
| `src/ccu/ccu_host_launch.cpp` | 使用 ACL Runtime 查询设备、显式传递设备 ID、按设备隔离缓存 |
| `src/ccu/CMakeLists.txt` | 查找并显式链接 `libascendcl.so` |
| `tests/ut/ccu/test_ccu_host_launch.cpp` | 更新 Runtime stub，增加设备和缓存相关测试 |

## 7. 兼容性和边界

- 对外的 `asccomm_ccu_host_kernel_launch` 函数签名不变。
- 不新增公共头文件接口，不改变调用方配置结构。
- 仍然依赖 hcomm 提供的 CCU Kernel 注册、Kernel Manager 和任务参数生成实现；本次
  变更仅移除 `HcclGetThreadDeviceId` 这一项依赖。
- `libascendcl.so` 成为 `asccomm_ccu` 的显式运行时依赖，部署时必须保证其可加载。

## 8. 验证结果

- CCU Host Launch 单元测试共 9 项，全部通过；
- `-Wall -Werror` 编译通过；
- 使用 aarch64 CANN 工具链构建完整 `libasccomm_ccu.so` 成功；
- 生成库的动态未解析符号中不存在 `HcclGetThreadDeviceId`；
- 生成库的动态依赖中包含 `libascendcl.so`；
- `git diff --check` 通过。

