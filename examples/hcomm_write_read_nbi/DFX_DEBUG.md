# DFX 双卡诊断

本分支用于比较同一进程、同一设备、实际传入 kernel 的 next.channel 在运行前后的可读性。
不同进程的相同虚拟地址不表示同一个分配对象。设备 PostSend 日志带 channel、slot 和 SQ 地址；
PollCq 日志通过 channel 和 cqVa 关联 CQE 的 entryIdx。

## 构建与运行

先安装本分支 asc-comm 头文件及库，确保样例实际包含这份代码。
在已加载 Ascend SDK 环境的服务器、当前样例目录执行：

```bash
cmake -S . -B build-normal -DASCENDC_DEBUG=ON -DHCOMM_DFX_TEST_TRAP=OFF
cmake --build build-normal -j
HCOMM_DFX_CHECK_METADATA=1 HCOMM_DFX_DUMP_TRACE=1 timeout 180s ./build-normal/demo

cmake -S . -B build-trap -DASCENDC_DEBUG=ON -DHCOMM_DFX_TEST_TRAP=ON
cmake --build build-trap -j
HCOMM_DFX_CHECK_METADATA=1 HCOMM_DFX_DUMP_TRACE=1 timeout 180s ./build-trap/demo
```

若用 tee 保存输出，先执行 `set -o pipefail`，否则命令退出码可能只是 tee 的结果。
PRE_KERNEL 和 POST_KERNEL 都记录 PID、rank、device、channel、源地址、读取长度、返回码。
诊断函数按实体中的真实指针读取 buffer 表及 SQ/CQ 上下文，不使用固定偏移。
设置 HCOMM_DFX_CHECK_METADATA 会增加 Host 读取，可能改变时序；请另外保留不设置该变量的对照日志。
正常模式没有异常时 DFX 会跳过采集；元数据诊断仍会执行。
trap 模式在通信与 Drain 成功后注入异常；预期异常、完整 dump 和数据校验必须都满足才能通过。
上下文读取失败不再显示普通 test pass，dump 返回失败但仍打印已取得的部分。

## 独立验证异常后的 kernel 执行能力

每条命令单独启动新进程，不与普通 dump 混合。basic 只写普通 device buffer；sq 从启动前取得的
SQ 基地址读取第一个 64B（不是原失败槽位），用于初步比较两类访问。它不能证明整个 SQ 都可读。
模式会跳过正式 dump，并以失败退出码结束，防止被误认为完整通信/DFX 测试通过。

```bash
HCOMM_DFX_PROBE=basic timeout 180s ./build-trap/demo
HCOMM_DFX_PROBE=sq timeout 180s ./build-trap/demo
```

分别核对 mallocRet、createStreamRet、syncRet、copyRet。basic 回读还校验写入常量。
若 basic 也失败，不能将失败归责于 WQE 地址；若 basic 成功而 sq 失败，再追查队列读取。
也可用 build-normal 执行同样命令作为无异常基线。

## 游标与采样

HCOMM_DFX_CURSOR_SOURCE 默认 aiv。SIMT 调用方必须显式设置：

```bash
export HCOMM_DFX_CURSOR_SOURCE=simt
```

同一进程混用两种实现时，当前环境变量选择不适合并发切换，应分别运行诊断。
ChannelEntity 没有 AIV/SIMT 区分标记，不能根据某个游标是否为零推断类型。
AIV 显示原始通道游标，SQ 采样使用最后完成 BB 索引之后的位置；SIMT 使用设备中的计数器。
TRACE 的 rawTail/sampleTail 分别表示原始值和转换后的采样起点。
AIV 的 16 位完成索引及 CQ 计数回绕存在歧义时，不应把采样当作硬件完成状态的绝对证明。
无法解析的范围返回失败。cqHead 表示预期完成数量，不保证对应 CQE 已由硬件生成。

已完成的 2/1/2/2 或 12/11/12/12 示例不再重复采样最后完成的 WQE。
没有未完成候选项时不打印 WQE/CQE，并不表示队列从未使用。
多 BB WQE 按 opcode 跨过续块，避免把续块当成独立描述符。
本改动不修复运行时内存访问错误，也不增加异常前的队列历史记录。

## Host 回归测试（不需要 Ascend SDK）

在仓库根目录执行：

```bash
c++ -std=c++17 -Wall -Wextra -Werror tests/host/test_hcomm_dfx_cursor.cpp -o /tmp/test_hcomm_dfx_cursor
/tmp/test_hcomm_dfx_cursor
c++ -std=c++17 -Wall -Wextra -Werror -Itests/host/stubs -Iinclude/aicore tests/host/test_hcomm_dfx_collect.cpp -o /tmp/test_hcomm_dfx_collect
/tmp/test_hcomm_dfx_collect
```

采集测试使用模拟 ACL，包含故意失败的读取，出现 ERROR 日志是预期结果；以进程退出码判断通过。
这些测试不能代替设备编译和真实故障注入验证。
