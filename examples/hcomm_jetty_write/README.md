# Hcomm Jetty WRITE 样例

本样例演示 AIV Kernel 通过 `hcomm_jetty.h` 中的 `HcommPeer`、`HcommJetty::Put` 和
`HcommJetty::Drain` 发起 URMA Jetty WRITE。每个 rank 向其他所有 rank 写入 256 字节，
远端数据落在按发送 rank 划分的接收槽位中。

样例代码采用与 `ain/basic_ring` 类似的单文件 ASC 组织方式：`hcomm_jetty_write.asc`
同时包含 Host 侧资源准备、kernel 启动和设备侧 kernel 实现。通信实现仍使用
`hcomm_jetty.h`，没有改为 AIN 接口。

## 目录结构

```text
hcomm_jetty_write
├── CMakeLists.txt
├── hcomm_jetty_write.asc  // Host、kernel 和共享定义
├── run.sh
├── README.md
└── README_en.md
```

## 样例流程

1. rank 0 将 `HcclRootInfo` 写入临时文件，其余 rank 轮询读取后初始化 HCCL 通信域。
2. Host 注册通信 buffer，并为每个非本 rank peer 获取 `COMM_ENGINE_AIV` 的 UBC_CTP channel。
3. Host 将 channel handle 写入 device Jetty table，同时下发本端 buffer 地址、外部提供的远端目标
   device 地址和远端 buffer index。
4. Host 只下发一个 kernel；Kernel 遍历所有 peer context，分别构造 `HcommPeer`/`HcommJetty`，调用 `Put` 写入 256 字节，再向独立 marker 地址调用一次 `PutValue`，最后调用 `Drain` 等待完成；Host 分别校验数据槽和 marker。
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

直接启动单个 rank：

```text
./build/hcomm_jetty_write <rank> <nranks> <root_info_file>
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
