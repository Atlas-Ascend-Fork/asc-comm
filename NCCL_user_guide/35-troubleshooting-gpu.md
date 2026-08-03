# 35 · Troubleshooting · GPU

> 原文：Troubleshooting → GPU troubleshooting

## GPU Direct

NCCL 的 GPU 间通信严重依赖 **GPU Direct**——即一个 GPU 用直接的点对点 PCI 消息与另一个设备（另一个 GPU 或网卡）直接通信的能力。

直接点对点 PCI 消息可能因多种原因失败或性能很差：组件缺失、虚拟机或容器配置不当、某些 BIOS 设置。

## GPU 到 GPU 通信

### peer-to-peer 显存访问

对 GPU 到 GPU 的流量，**当 CUDA 报告 GPU 之间可以直接访问对方显存时，NCCL 优先选择 peer-to-peer transport**（通常经 NVLink，或在拓扑和驱动允许时经 PCIe）。

### 如何检查

```bash
nvidia-smi topo -p2p <capability>
```

打印 GPU 两两之间的 P2P 状态矩阵。`<capability>` 取值：`p` 表示 PCIe，`n` 表示 NVLink。

健康的 8 GPU PCIe 系统（全 P2P 可达）矩阵中，非对角线位置应全为 `OK`，对角线为 `X`。

### 常见陷阱：ACS

**如果 `nvidia-smi topo -p2p` 对你使用的 GPU 对显示 `OK`，但 NCCL 的 P2P 仍不按预期工作，一个常见原因是 PCI Access Control Services（ACS）**，见下文。

### CUDA sample 验证

`cuda-samples` 仓库里的 **`simpleP2P`**（路径 `Samples/0_Introduction/simpleP2P`）会在每次传输后校验数据，确认拷贝成功。**运行成功的最后一行应该是 `Test passed`。**

### 测带宽

要测量 GPU 之间的可用带宽，**推荐用 [nvbandwidth](https://github.com/NVIDIA/nvbandwidth) 而不是上面的 CUDA sample**。

### 隔离 P2P 问题

想判断问题是否与 P2P transport 相关，可以对比「禁用或限制 P2P 显存访问」前后的运行结果：

- `NCCL_P2P_DISABLE`
- `NCCL_P2P_LEVEL`

（详见 `33-environment-variables.md`）

## GPU 到 NIC 通信

GPU 也可以通过 **GPU Direct RDMA（GDRDMA）** 直接与网卡通信。要求：

- 兼容的网卡和驱动
- 加载额外的内核模块 **`nvidia-peermem`**

`nvidia-peermem` 模块现在随 CUDA 驱动提供，但**每次节点启动都必须加载**：

```bash
sudo modprobe nvidia-peermem
```

若没有 sudo 权限，可以这样验证模块是否已加载：

```bash
lsmod | grep nvidia-peermem
```

**替代路径**：GDRDMA 也可以通过较新 Linux 内核的 **DMA-BUF** 特性配合**开源 NVIDIA GPU 驱动**启用。这种情况下 **NCCL 会自动检测并启用 DMA-BUF，就不需要 `nvidia-peermem` 模块了**。

## PCI Access Control Services（ACS）

### 裸金属系统

**IO 虚拟化（也叫 VT-d 或 IOMMU）会干扰 GPU Direct**：它把所有 PCI 点对点流量重定向到 CPU root complex，造成显著性能下降甚至 hang。

检查 PCI bridge 上是否启用了 ACS：

```bash
sudo lspci -vvv | grep ACSCtl
```

**若输出中出现 `SrcValid+`，则 ACS 可能已启用。** 看 `lspci` 的完整输出可以确认具体哪个 PCI bridge 启用了 ACS：

```bash
sudo lspci -vvv
```

**若 PCI switch 启用了 ACS，需要把它关掉。** 某些系统可以在 BIOS 里通过禁用 IO virtualization 或 VT-d 来做。对于 Broadcom PLX 设备，可以从 OS 侧关闭，**但每次重启后都要重做一遍**。

找出 PLX PCI bridge 的 PCI bus ID：

```bash
sudo lspci | grep PLX
```

用 `setpci` 关闭 ACS，把 `03:00.0` 换成各个 PCI bridge 的 PCI bus ID：

```bash
sudo setpci -s 03:00.0 ECAP_ACS+0x6.w=0000
```

或者用脚本批量处理：

```bash
for BDF in `lspci -d "*:*:*" | awk '{print $1}'`; do
  # skip if it doesn't support ACS
  sudo setpci -v -s ${BDF} ECAP_ACS+0x6.w > /dev/null 2>&1
  if [ $? -ne 0 ]; then
    continue
  fi
  sudo setpci -v -s ${BDF} ECAP_ACS+0x6.w=0000
done
```

### 虚拟机

**虚拟机需要 ACS 才能工作，因此关闭 ACS 不是一个选项。** 要在虚拟机内获得最佳性能，需要**在网络适配器上启用 ATS**。

## 拓扑检测

NCCL 依赖 **`/sys`** 来发现 GPU 和网卡的 PCI 拓扑。

- 在虚拟机或容器内运行时，**确保 `/sys` 已正确挂载**。
- **`/sys` 暴露的是虚拟 PCI 拓扑会导致性能次优。**

## 排查清单

| 症状 | 先查什么 |
| --- | --- |
| GPU 间性能差或 hang | `nvidia-smi topo -p2p p/n` → ACS（`lspci -vvv \| grep ACSCtl`）→ BIOS 的 IOMMU/VT-d |
| P2P 显示 OK 但 NCCL 仍不走 P2P | ACS |
| 想确认是否 P2P 相关 | 用 `NCCL_P2P_DISABLE` / `NCCL_P2P_LEVEL` 对比 |
| GPU-NIC 走不通 | `lsmod \| grep nvidia-peermem`；或确认 DMA-BUF + 开源驱动路径 |
| 容器/VM 内性能次优 | `/sys` 是否正确挂载、是否是虚拟拓扑；VM 内是否启用了 ATS |
| 想测实际带宽 | nvbandwidth |
