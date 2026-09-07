# Hcomm Jetty WRITE 性能样例

本样例演示 AIV Kernel 通过 `hcomm_jetty.h` 中的 `HcommPeer`、`HcommJetty::Write` 和
`HcommJetty::Drain` 发起 URMA Jetty WRITE。每个 rank 向其他所有 rank 重复提交 1000 次 256
字节的 `Write`，计算 1000 次提交的平均接口耗时（设备周期），不把循环结束后的 `Drain` 等待时间计入；远端数据
仍落在按发送 rank 划分的接收槽位中。

样例代码采用与 `ain/basic_ring` 类似的单文件 ASC 组织方式：`hcomm_jetty_write_perf.asc`
同时包含 Host 侧资源准备、kernel 启动和设备侧 kernel 实现。通信实现仍使用
`hcomm_jetty.h`，没有改为 AIN 接口。

## 目录结构

```text
hcomm_jetty_write_perf
├── CMakeLists.txt
├── hcomm_jetty_write_perf.asc  // Host、kernel 和共享定义
├── run.sh
├── README.md
└── README_en.md
```

## 样例流程

1. rank 0 将 `HcclRootInfo` 写入临时文件，其余 rank 轮询读取后初始化 HCCL 通信域。
2. Host 注册通信 buffer，并为每个非本 rank peer 获取 `COMM_ENGINE_AIV` 的 UBC_CTP channel。
3. Host 将 channel handle 写入 device Jetty table，同时下发本端 buffer 地址、外部提供的远端目标
   device 地址和远端 buffer index。
4. Host 只下发一个 kernel；Kernel 遍历所有 peer context，在 1000 次 `Write` 提交循环前后记录 `asc_get_system_cycle()` 并计算平均值，最后调用一次 `Drain` 等待完成。
5. 所有 rank 通过 HCCL barrier 同步，Host 回读 buffer 并校验每个 peer 的数据 pattern。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
cmake -S . -B build -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build -j
```

也可以将 `/usr/local/Ascend/cann` 替换为实际 CANN 安装目录。

## 运行

```bash
./run.sh <nranks>
```

例如在本机两张卡上运行：

```bash
./run.sh 2
```

每个 rank 会输出每个 peer 的 1000 次 `Write` 平均耗时，单位为设备 cycle；计时区间只包围提交循环，不包含 `Drain`。

直接启动单个 rank：

```text
./build/hcomm_jetty_write_perf <rank> <nranks> <root_info_file>
```

所有 rank 输出 `Jetty WRITE validation passed` 表示校验成功。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 产品 | `Ascend 950PR / Ascend 950DT` |
| NPU 架构 | `dav-3510` |
| 通信引擎 | `COMM_ENGINE_AIV` |
| 通信协议 | `COMM_PROTOCOL_UBC_CTP` |
| 部署形态 | 单机多卡 |
