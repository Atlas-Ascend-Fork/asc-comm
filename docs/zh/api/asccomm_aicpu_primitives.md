# asccomm_aicpu 数据面接口实现逻辑

`libasccomm_aicpu.so` 是 asc-comm 新增的 AICPU 数据面共享库，通过 C API 向 AICore 程序提供跨模组通信（HCCS/RoCE/UB）能力。仅导出 `asccomm_*` 符号，其余符号全部 hidden。

## 架构分层

```
API 层 (aicpu_ts_asccomm_primitives_c_adpt.cc)  — C 接口 + 参数校验 + POD ABI 校验
  │
  ├─ Launch 层 (asccomm_launch.cc)            — batch 模式、线程注册、任务批量发射
  ├─ Runtime 层 (asccomm_runtime.cc)          — 本地 RTSQ：sdma copy/reduce、notify、任务发射
  └─ Transport 层 (asccomm_transport.cc)      — 通道通信：UB / RDMA 两种协议
        ├─ ub_conn_lite.cc         — 构造 UB(doorbell) SQE 写入 jetty 队列
        ├─ rdma_vendor_1825_ops.cc — RDMA WQE 构造 + 门铃 + CQ 轮询
        └─ sqe_build_a5.cc         — 生成 A5 平台 RTSQ 的 SQ_TAIL 门铃 SQE
```

依赖关系：两个 `.so`（`libasccomm_aicpu.so` 与 hcomm 的 `libccl_kernel.so`）之间**只有编译期头文件依赖**（`pkg_inc/hcomm/*.h`），无运行时链接依赖，完全解耦。

## 通用机制

### `execute_api`
每个接口都用 `execute_api(std::function)` 包裹，捕获所有异常并统一转成 `HCCL_E_INTERNAL`。

### POD ABI 校验
所有接口先通过 hcomm 提供的 `HcommGetPodThreadResource(thread, &entity)` / `HcommGetPodChannelResource(channel, &entity)` 获取实体，再校验 `abiHeader.version / magicWord / size`，确认与库编译时的 POD 数据结构版本一致。

### handle → 实体解析
- `get_thread_entity(thread, registerThread, entity)`：取线程实体，`registerThread=true` 时把线程加入 batch 注册表。
- `get_context(thread, channel, registerThread, context)`：在线程实体基础上再取通道实体。
- `get_thread_notify_id`：从线程实体 `localNotifyAddr[]` 查 IPC_RT 类型的 notify 标号。

### reduce 参数转换
`convert_reduce_in` / `get_reduce_params`：把 `HcommDataType`/`HcommReduceOp` 映射为内部 `Asc::DataType`/`Asc::ReduceOp`，并计算字节长度 `len = count * typeSize`（带溢出校验）。

---

## 1. Batch 模式（asccomm_launch.cc）

`LaunchState`（thread_local）：记录已注册线程列表、notify 超时、SQ 满超时、batch 标志。

### `asccomm_batch_mode_start / end`
- **start**：置 `batchMode=true`。此后所有接口只把 SQE 累积到本地 RTSQ 缓冲，不立即发射。
- **end**：置 `batchMode=false`，一次 `asccomm_launch_registered_threads()` 把所有注册线程的 RTSQ 统一批量发射。

辅助函数：
- `asccomm_register_thread`：batch 模式下把线程去重登记。
- `asccomm_launch_registered_threads` / `asccomm_try_launch_registered_threads`：对注册线程逐个执行 `asccomm_task_launch` / `asccomm_try_launch`。

---

## 2. 本地操作（无通道，仅 RTSQ）

### `asccomm_local_copy_on_thread(thread, dst, src, len)`
在指定线程的 RTSQ 上排队一个 SDMA copy SQE。按 `0x100000000`(4GB) 分片循环执行。
链路：`asccomm_rtsq_sdma_copy` → `build_a5_sqe_sdma_copy` → `refresh_rtsq`。

### `asccomm_local_reduce_on_thread(thread, dst, src, count, dataType, reduceOp)`
同上，但 SQE 带 reduce op + dataType（映射为 RT_STARS 的 op/dataType 编码）。长度按 `count * typeSize` 计算并分片。

---

## 3. Notify（线程级信号，RTSQ 上执行）

### `asccomm_thread_notify_record_on_thread(thread, dstThread, dstNotifyIdx)`
从 `dstThread` 实体按 `dstNotifyIdx` 查 `localNotifyAddr[]` 的 IPC_RT notify 标号，在源线程 RTSQ 上排队 record SQE。

### `asccomm_thread_notify_wait_on_thread(thread, notifyIdx, timeOut)`
在本线程 RTSQ 排队 wait SQE，等待本线程实体中索引为 `notifyIdx` 的本地 notify，带超时。

### `asccomm_aclrt_notify_record / wait_on_thread`
与上类似，但直接传 `notifyId`，不查询实体 notify 表（用于 aclrt 侧信号）。

---

## 4. 传输（写/读，走通道）

| 接口 | 底层函数 | 说明 |
|---|---|---|
| `asccomm_write_on_thread` | `asccomm_write` | 按通道协议分派 UB / RDMA |
| `asccomm_write_reduce_on_thread` | `asccomm_write_reduce` | 写 + reduce |
| `asccomm_write_with_notify_on_thread` | `asccomm_write_with_notify` | 写 + 写后通知 |
| `asccomm_write_reduce_with_notify_on_thread` | `asccomm_write_reduce_with_notify` | 写 + reduce + 通知 |
| `asccomm_read_on_thread` | `asccomm_read` | 远端读 |
| `asccomm_read_reduce_on_thread` | `asccomm_read_reduce` | 读 + reduce（RoCE 平台为 no-op） |

每个接口流程统一：
1. 校验指针、转换 reduce 参数。
2. `get_context` 取线程+通道实体。
3. 按 `channel.protocol` 分派：
   - **UB 协议**（UBC_CTP / UBC_TP / UB_MEM / UBOE / UBG）→ `execute_ub_rma`
   - **RDMA 协议**（ROCE）→ `execute_rdma`

---

## 5. execute_ub_rma（UB 门铃引擎）

1. 取通道的 **JFS（Jetty Fast-path Send）上下文** `HcommJfsContext`。
2. **`build_ub_slices`**：在注册 buffer 表中查找本地/远端 buffer（要求 URMA 保护类型），构造：
   - `RmaBufSliceLite`（本地，带 tokenId）
   - `RmtRmaBufSliceLite`（远端，带 tokenId/tokenValue）
3. **`fill_ub_param` → `import_context`**：装载 `dieId / funcId / jettyId / sqVa / sqDepth / tpn / maxReadSize / maxWriteSize / 远端EID`。
4. **`import_ub_state`**：恢复上次的 `pi`（队列写指针）。
5. 执行具体操作（write / reduce / notify）。
6. **`export_ub_state`**：导出最新 `pi`。
7. **`ring_ub_doorbell`**：构造 UB jetty 门铃，排 RTSQ SQE（`build_a5_sqe_ub_db_send`）敲 doorbell。

### `UbConnLite` 内层
把大块按 `maxReadSize / maxWriteSize` 切片，每片构造一个 `UdmaSqeWrite` / `UdmaSqeWriteWithNotify`，写入 jetty 的 SQ VA：
- 带 notify 的 SQE 占用 96 字节（2 个 wqebb 共 128 字节），循环回绕时特殊处理。
- `fill_comm_sqe` 统一填充 opcode / 保序（placeOdr）/ 完成（compOrder）/ fence / 远端 EID / token / 地址。
- `fill_comm_sqe_reduce_info` 填充 inline reduce 的 opcode 和 dataType 编码并置 `udfFlag`。
- `process_slices` 负责计算切片数与切片位置（FIRST/MIDDLE/LAST/ONLY），用于保序与完成配置。

---

## 6. execute_rdma（RDMA 引擎）

1. 取通道的 **ROCE SQ/CQ 上下文**。
2. `build_rdma_context`：装载 `qpn / sqVa / wqeSize / depth / dbHwVa / dbSwVa / cqVa / cqDepth` 等。
3. `Rdma1825Ops`：import 队列状态，按 `RDMA_DMA_MAX_SIZE`(2GB) 分片，执行 `read / write / write_reduce`。
4. `build_rdma_notify_slices`：写后通知场景，从 buffer 表取 notify 值 buffer，构造额外的 notify 写。
5. `build_doorbell` → `asccomm_rtsq_rdma_db_send` 敲一次门铃。
6. **`poll_cq`** 轮询完成队列确认（`WRITE_WITH_NOTIFY` 需 poll 2 个 CQ 项）。
7. export 队列状态写回实体（`sqHead / sqTail / cqHead / cqTail / cqDbFlush`）。

---

## 7. 通道级 Notify / Fence / Drain

### `asccomm_notify_record`
向远端写入一个 notify 值（`UB` 用 `inline_write` 写 `1` 到远端 notify 地址；`notifyIdx==1` 时加强序保证）。RDMA 走 `RdmaOperation::NOTIFY_RECORD`。

### `asccomm_channel_notify_wait_on_thread`
从通道本地 notify 表取 notifyId，排 RTSQ wait SQE。

### `asccomm_fence`
只在通道 SQ 上下文置 `fence=1` 标志。真正生效在**下一次传输**时：
- UB：`get_ub_config` 读到 fence → 置强序 + 完成。
- RDMA：置 `cfg.fence`。
随后复位 `fence=0`。

### `asccomm_drain`
排空通道。用远端 drain buffer 做一次强序 read，再等待本地 drain notify。仅支持 UB（RoCE 直接返回成功）。

---

## 8. 批量传输（asccomm_batch_transfer，仅 UB）

1. 装载一次 `UbConnLite` 连接上下文。
2. 遍历 `HcommBatchTransferDesc` 数组，按 `transType`（WRITE / READ / WRITE_REDUCE / WRITE_WITH_NOTIFY / WRITE_REDUCE_WITH_NOTIFY / NOTIFY_RECORD / READ_REDUCE）解析本地/远端地址与 reduce 参数。
3. 每项 `build_ub_slices` 查 buffer，调用对应 `connection.write / read / write_reduce / ...`。
4. 最后一项才开 `cqeEn` + 强序（保证完成）。
5. 全部完成后一次性导出 `pi` 并**敲一次门铃**批量执行所有描述符。

---

## 9. 超时设置

### `asccomm_set_notify_wait_timeout` / `asccomm_thread_res_acquire_timeout`
把 `float` 秒转换为 `uint32`（校验 NaN / 负数 / 溢出），存入 thread_local `LaunchState`：
- notify 超时：用于 wait SQE。
- SQ 满超时（`sqFullTimeout`）：用于 RTSQ 空间自旋等待。

---

## Runtime 层关键辅助（asccomm_runtime.cc）

所有本地 RTSQ 操作最终汇聚到此层：

- **`refresh_rtsq`**：递增 pending SQE 计数；满足「非 batch 模式」或「攒满 `HCOMM_POD_RTSQ_BATCH_SIZE`」时触发 `asccomm_rtsq_launch`。
- **`asccomm_rtsq_launch`**：`ensure_queue_space` → `copy_pending_sqes`（把本地缓冲 SQE 拷到硬件 SQ VA，处理回绕）→ `config_sq_tail` 更新 SQ_TAIL（敲一次硬件门铃）→ 清零本地缓冲。
- **`ensure_queue_space`**：按 SQ_HEAD 距离自旋等待空间；超时返回 `HCCL_E_TIMEOUT`；等待期间可 `try_launch` 已注册线程。
- **`acquire_task_id`**：从 `aicpu::GetSqeId`（weak 符号，由上层 AICore 程序提供）批量申请 taskId，内存并递增。
- **`asccomm_task_launch` / `asccomm_try_launch`**：对所有线程实体执行强制/条件发射。

---

## 相关文件

| 文件 | 职责 |
|---|---|
| `src/aicpu/hcomm/primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc` | C API 实现 + 参数/ABI 校验 |
| `src/aicpu/hcomm/primitives/launch/asccomm_launch.cc` | batch 模式、线程注册、批量发射 |
| `src/aicpu/hcomm/resource/common/asccomm_runtime.cc` | 本地 RTSQ、任务发射、SQ 尾更新 |
| `src/aicpu/hcomm/resource/common/asccomm_transport.cc` | UB/RDMA 传输分派与通道操作 |
| `src/aicpu/hcomm/resource/connection/ub_conn_lite.cc` | UB SQE 构造与 jetty 队列写入 |
| `src/aicpu/hcomm/resource/connection/rdma_vendor_1825_ops.cc` | RDMA WQE 构造、门铃、CQ 轮询 |
| `src/aicpu/hcomm/resource/stream/sqe_build_a5.cc` | A5 平台 RTSQ 门铃 SQE 生成 |
