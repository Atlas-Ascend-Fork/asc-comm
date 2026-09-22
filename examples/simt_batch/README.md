# Hcomm SIMT Batch 最小示例

该示例展示单Lane显式Batch的最小完整用法：创建`BatchHandle`，执行一次
`WriteNbi → BatchCommit`，最后调用`Drain`等待完成并校验远端数据。

Kernel侧核心流程：

```cpp
constexpr uint32_t kWorkspaceBytes = AscendC::simt::HcommSimtBatchBufferBytes(1U);
alignas(128) __ubuf__ uint64_t workspace[kWorkspaceBytes / sizeof(uint64_t)];

AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0U) != 0) {
    return;
}
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), kWorkspaceBytes, remote, 1U);
if (hcomm.WriteNbi(batch, remote, local, 64U) != 0 || hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.Drain(batch) != 0) {
    return;
}
```

BatchHandle绑定`remote`所在的远端注册区，后续目标地址必须位于同一注册区。最后一个参数`1U`
表示本示例中的Write WQE占用1个WQEBB。

## 构建与运行

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch
bash build.sh
bash run.sh                 # 2卡
bash run.sh --devices 8     # 8卡，按0→1、2→3、4→5、6→7配对
```

示例使用`HcclGetRootInfo`和`HcclCommInitRootInfo`创建通信域，不需要rank table。全部rank的接口返回值和
接收端数据均正确时，脚本输出`Status=PASS`并返回0。
