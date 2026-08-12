# Hcomm共享Jetty Batch Write双卡样例

本样例验证AIV Kernel在两个共享Jetty Channel上分别组装一个`WriteNbi` WQE，再通过一次
`BatchCommit`写入共享SQ并触发一次Doorbell，最后通过`Drain`等待两个CQE。

## 验证范围

样例固定使用同一主机上的两个rank和两张NPU。每个rank都向唯一的对端创建两个逻辑Channel，两个Channel
具有相同的本端Endpoint和对端Endpoint，由`MakeMultiChannelHandle`配置为共享本端Jetty。Kernel执行以下流程：

```cpp
auto batch = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferSize);
auto channel0 = hcomm.GetHandleRef(batch, 0);
auto channel1 = hcomm.GetHandleRef(batch, 1);

hcomm.WriteNbi(channel0, remoteBuffer + recvOffset0, localBuffer + sendOffset0, dataSize);
hcomm.WriteNbi(channel1, remoteBuffer + recvOffset1, localBuffer + sendOffset1, dataSize);
hcomm.BatchCommit(batch);
hcomm.Drain(batch);
```

两个Channel写入不同的远端Buffer分片。Kernel完成后，Host逐字节校验两个分片，确保两条WQE均已执行。

双卡可以验证：

- Host共享Channel创建和`MultiChannelHandle`下发；
- `remoteIndex=0/1`解析两个不同的Channel实体；
- 多条WQE在UB中暂存后统一进入共享SQ；
- SQ回收和两个CQE完成状态；
- 同一个peer上的多个逻辑Channel共享Jetty。

双卡不能验证多个不同peer共享一个本端Jetty。该拓扑至少需要三个rank，使一个rank同时连接两个不同对端。

## 环境要求

- Ascend 950PR或Ascend 950DT，至少两张NPU；
- 已安装支持`HcclChannelAcquireWithConfig`共享Jetty控制面的HCOMM包；
- 已将`origin/run`的开发run包装包逻辑与当前Batch实现合并，并将生成的run包安装到当前CANN环境；
- 已执行CANN环境脚本，`ASCEND_HOME_PATH`指向上述环境。

开发run包将Device公开头和实现头分别安装到：

```text
asc/include/adv_api/hcomm
asc/impl/adv_api/detail/hcomm
aarch64-linux/include/hcomm
```

Host辅助接口为header-only实现，样例直接使用开发run包安装到CANN中的头文件，不再构建额外的Host动态库。

## 编译运行

```bash
cmake -S examples/hcomm_batch_write -B build/examples/hcomm_batch_write
cmake --build build/examples/hcomm_batch_write -j
./build/examples/hcomm_batch_write/hcomm_batch_write
```

程序通过`fork`启动两个rank，并使用本地`socketpair`交换RootInfo、远端Device地址及完成同步，因此当前版本仅支持
单机双卡验证。成功时输出：

```text
rank 0: shared-Jetty batch write passed
rank 1: shared-Jetty batch write passed
hcomm batch write test passed
```
