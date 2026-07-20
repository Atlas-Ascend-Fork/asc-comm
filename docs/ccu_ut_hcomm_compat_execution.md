# CCU UT hcomm 兼容层执行记录

## 目标

CCU 数据面 UT 不再依赖 sibling `hcomm` 源码仓。测试仅依赖本仓源码、`tests/ccu/hcomm_compat` 和 CANN 安装包。

## 兼容层边界

`tests/ccu/hcomm_compat/include` 中的内容按以下规则维护：

- 可复制的独立内容：`hcomm_primitives.h` 的 handle/C 枚举、`task_param.h` 的 profiling 记录、datatype/reduce 的 enum wrapper、字符串格式化、异常和日志宏。
- 仅测试桩的业务对象：`CcuUrmaChannel`、`CcuDevMgrImp`、`CcuKernel`。兼容头只提供目标编译需要的最小声明；行为由 `tests/ccu/stub` 提供，不复制 hcomm 通道、设备资源或 kernel 实现。
- 为兼容 CCU 源码中的相对 include，`hcomm_compat/source` 只保存路径映射头，不保存 hcomm 产品源码。

## 构建改动

- `tests/ut/CMakeLists.txt` 删除 hcomm 源码仓探测、遗留变量别名和 hcomm include 路径。
- `tests/ccu/hcomm/CMakeLists.txt` 删除 hcomm 头文件递归扫描，并用 `asccomm_ccu_hcomm_compat` 向 UT target 注入兼容层。
- 所有 CCU UT 从本仓 `src/ccu/dataplane/hcomm` 编译已迁移的 CCU 源码。

## 验证

2026-07-20 在全新目录完成：

```bash
cmake -S tests/ut -B build/ut-ccu-standalone \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DCANN_3RD_LIB_PATH="$PWD/third_party"
cmake --build build/ut-ccu-standalone -j4
ctest --test-dir build/ut-ccu-standalone/ccu_hcomm --output-on-failure
```

结果：9/9 CCU CTest 通过。CMake 配置输出不再探测或打印 hcomm 源码仓路径。
