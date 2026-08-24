# Hcomm共享Jetty Batch Write样例

## 功能说明

本样例演示多个`COMM_PROTOCOL_UBC_CTP` Channel共享一个Jetty。Rank 0按照本端Endpoint对peer进行分组，
每组创建一个`MultiChannelHandle`；AIV Kernel将组内多个peer的`WriteNbi`任务写入同一批次，通过一次
`BatchCommit`提交到共享SQ，再调用`Drain`等待完成。接收端最终校验Rank 0写入的数据。

Kernel侧主要流程如下：

```cpp
auto multiBatchHandle = hcomm.MakeBatchHandle(multiChannel, batchBuffer, batchBufferSize);
for (uint32_t index = 0; index < channelNum; ++index) {
    GM_ADDR remoteAddr = remoteBuffers[index] + recvOffset;
    auto& peerBatchHandle = hcomm.GetHandleRef(multiBatchHandle, index, remoteAddr);
    hcomm.WriteNbi(peerBatchHandle, remoteAddr, localBuffer, dataSize);
}
hcomm.BatchCommit(multiBatchHandle);
hcomm.Drain(multiBatchHandle);
```

`channelIndex`与Host侧传入`MakeMultiChannelHandle`的`channelDescs`数组下标一致。`GetHandleRef`根据该索引和远端地址选择并缓存MR token，返回内层BatchHandle引用；批量写使用该引用，`BatchCommit`和`Drain`使用外层多通道批量句柄。

## 支持环境

- Ascend 950PR或Ascend 950DT；
- 至少两张NPU，仅支持单机多卡；
- 当前CANN环境已安装包含共享Jetty Batch接口的asc-comm包。

使用三张及以上NPU时，Rank 0可以构造面向多个peer的Batch；实际分组数量由RankGraph中的本端Endpoint决定。

## 编译运行

先配置CANN环境：

```bash
source ${install_path}/cann/set_env.sh
```

一键编译并运行，默认使用两张NPU：

```bash
bash examples/hcomm_batch_write/run.sh
bash examples/hcomm_batch_write/run.sh 4
```

也可以手动执行：

```bash
cmake -S examples/hcomm_batch_write -B build/examples/hcomm_batch_write \
    -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build build/examples/hcomm_batch_write -j
./build/examples/hcomm_batch_write/hcomm_batch_write 4
```

成功时最终输出：

```text
hcomm 4-rank grouped batch write test passed
```
