# CCU UT Data 适配记录

## 1. 目标

在不修改 `src/` 产品源码的前提下，使 `reps/data` UT 使用唯一、可配置的 channel 查询桩，消除测试文件与公共 stub 对 `HcommChannelGet` 的重复定义。

## 2. 问题

原 data UT 在 `ut_ccu_rep_data_test.cc` 内定义 `HcommChannelGet`，同时 `tests/ccu/stub/exception_stub.cc` 也定义该符号。二者被链接到同一个测试目标时会造成重复定义风险。

此外，原 `MockCcuUrmaChannel` 不是 `Channel`/`CcuUrmaChannel` 实例，无法满足数据面实现中的：

```cpp
dynamic_cast<CcuUrmaChannel *>(static_cast<Channel *>(channelPtr))
```

## 3. 测试侧适配

新增测试辅助接口：

```text
tests/ccu/stub/ccu_channel_get_stub.h
tests/ccu/stub/ccu_channel_get_stub.cc
```

`HcommChannelGet` 仅在该 stub 中定义一次，定义在全局作用域以匹配实际接口声明。测试可通过以下接口注册或清理 channel：

```cpp
hcomm::SetHcommChannelGetStub(Channel *channel)
hcomm::ResetHcommChannelGetStub()
```

data UT 删除自定义 `HcommChannelGet` 和伪 channel 类，改为构造真实类型：

```cpp
CcuUrmaChannel(nullptr, HcommChannelDesc{})
```

并将其注册到公共查询 stub。这样产品代码中的 `dynamic_cast<CcuUrmaChannel *>` 可安全成功。

`ccu_channel_stub.cc` 增加数据 REP 所需的最小实现：

- `GetDieId()`。
- `GetLocXnByIndex()`。
- `GetRmtBuffer()`，返回固定的地址、大小和 token 值。

`reps/data` CMake 追加 `${CCU_IMPL_INCLUDE_DIRS}`，用于解析原始 `CcuRepContext` 所需的 hcomm channel 头文件。

## 4. 验证

执行：

```bash
source "$ASCEND_HOME_PATH/set_env.sh"
cmake -S tests/ut -B build/ut-hcomm \
  -DASCEND_CANN_PACKAGE_PATH="$ASCEND_HOME_PATH" \
  -DCANN_3RD_LIB_PATH="$PWD/third_party"
cmake --build build/ut-hcomm --target hccl_utest_comms_ccu_rep_data -j8
build/ut-hcomm/ccu_hcomm/ccu_representation/reps/data/hccl_utest_comms_ccu_rep_data
```

结果：`hccl_utest_comms_ccu_rep_data` 的 33 项测试全部通过。

## 5. 覆盖缺口

本次适配解决了构建、链接和 channel 对象类型问题，但既有的以下 remote REP `Translate` 用例当前仍只断言 `Type()`：

- `CcuRepRead`。
- `CcuRepWrite`。
- `CcuRepBufRead`。
- `CcuRepBufWrite`。
- `CcuRepRemMem`。

后续应使用当前可配置的 channel stub 补充这些 remote 翻译路径对指令、channelId、远端 buffer 地址和 token 的断言。
