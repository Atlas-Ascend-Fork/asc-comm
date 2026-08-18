# Hcomm共享Jetty Batch Write单机多卡样例

本样例验证AIV Kernel将同一本端通信地址下、面向不同peer的多个Channel组成Batch，通过一次
`BatchCommit`写入共享SQ并触发Doorbell，最后通过`Drain`等待该组全部CQE。

## 验证范围

卡数由命令行参数指定，默认使用两张NPU。rank 0从RankGraph中为rank 1至rank N-1选择
`COMM_PROTOCOL_UBC_CTP`链路，并按照`localEndpoint.commAddr.raws`分组；其他rank只建立到rank 0的链路。
每组通过一次`MakeMultiChannelHandle`创建共享Jetty Channel，不同组使用不同`sharedQueueTag`。

Kernel针对每个组执行以下流程：

```cpp
auto batch = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferSize);
for (uint32_t index = 0; index < channelNum; ++index) {
    auto channel = hcomm.GetHandleRef(batch, index);
    hcomm.WriteNbi(channel, remoteBuffers[index] + recvOffset, localBuffer, dataSize);
}
hcomm.BatchCommit(batch);
hcomm.Drain(batch);
```

所有Channel创建完成后，只有rank 0按组执行Kernel，向rank 1至rank N-1写入数据。`Drain`完成并经过
第二次Barrier后，其他rank逐字节校验rank 0写入的数据。

样例可以验证：

- Host共享Channel创建和`MultiChannelHandle`下发；
- 不同peer按照本端`commAddr.raws`分组；
- 同一组的不同peer Channel共享一个Jetty；
- 不同`raws`组使用不同Jetty；
- 多条WQE在UB中暂存后统一进入共享SQ；
- SQ回收和组内全部CQE完成状态。

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
./build/examples/hcomm_batch_write/hcomm_batch_write 4
```

程序通过`fork`启动指定数量的rank，rank与本机device编号一一对应。父进程使用本地`socketpair`分发RootInfo、
汇总所有Device地址并执行全局Barrier。该样例只支持单机多卡。成功时输出类似：

```text
rank 0: endpoint group 0 has 3 peer(s)
rank 1: received and verified data from rank 0
hcomm 4-rank grouped batch write test passed
```
