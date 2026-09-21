#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
"""Syntax-check full example sources, not device execution or SDK ABI compatibility."""

from pathlib import Path
import re,subprocess
root=Path(__file__).resolve().parents[4]
s=(root/'examples/multi_jetty_batch_perf/simt_kernel.cpp').read_text()
common=(root/'examples/multi_jetty_batch_perf/multi_jetty_batch_perf_common.h').read_text()
inner=(root/'src/aicore/hcomm/common/hcomm_simt_inner_def.h').read_text()
types=inner[inner.index('// Completion state is caller-owned'):inner.index('template <typename T>\nstruct ChannelTraits;')]
prefix=r'''
#include <cstdint>
#include <cstddef>
#include <ctime>
#define __simt_callee__
#define __simt_vf__
#define __gm__
#define __ubuf__
#define __global__
#define __vector__
#define __launch_bounds__(n)
void asc_threadfence_block(); void asc_threadfence(); void asc_dcci_single(void*);
struct dim3 { unsigned x; dim3(unsigned n):x(n){} };
extern dim3 threadIdx;
namespace cooperative_groups {
struct Group { unsigned thread_rank() const; unsigned size() const; void sync() const; };
template<unsigned> struct block_tile_memory { char data[16]; };
template<unsigned N> Group this_thread_block(block_tile_memory<N>&);
Group this_thread_block(); Group coalesced_threads();
template<unsigned N> Group tiled_partition(Group);
Group tiled_partition(Group,unsigned);
}
namespace AscendC {
constexpr int HCOMM_SUCCESS=0,HCOMM_FAILED=-1,COMM_PROTOCOL_UBC_CTP=0,HCOMM_URMA_DEFAULT_QP_IDX=0;
using ChannelHandle=uint64_t;
struct SqContext { struct { struct { uint64_t sqVa,headAddr,tailAddr; uint32_t jfsID,sqDepth; } ubJfs; } contextInfo; };
struct CqContext { struct { struct { uint64_t scqVa,tailAddr; uint32_t jfcID,cqeSize,cqDepth; } ubJfc; } contextInfo; };
namespace simt {
struct HcommSimtChannelEntity { uint64_t sqContextAddr,cqContextAddr; };
struct HcommSimtBatchStaticContext { uint64_t reservedHead; uint32_t itemBb; int32_t reservationStatus; };
struct HcommSimtResolvedPost { uint64_t* headAddr; uint64_t dwqeAddr; };
void HcommSimtFlushBatchLaneSq(HcommSimtBatchStaticContext*,uint32_t,uint32_t);
void HcommSimtLoadBatchStaticPost(HcommSimtBatchStaticContext*,HcommSimtResolvedPost&);
uint32_t HcommSimtBatchTotalBb(uint32_t,uint32_t);
void HcommSimtPlanBatchFromHead(HcommSimtBatchStaticContext*,uint64_t);
uint64_t* HcommSimtBatchPublisherWords(HcommSimtBatchStaticContext*);
void HcommSimtRingBatchDwqe(uint64_t,uint64_t*);
struct UbcCtpBatchHandle { void* context; };
uint8_t* RemoteBufferAddr(uint64_t,uint32_t); uint8_t* LocalBufferAddr(uint64_t,uint32_t);
'''+types+r'''
constexpr unsigned HcommSimtBatchBufferBytes(unsigned) { return 256; }
bool HcommSimtCompletionReady(const UbcCtpCompletionSet&);
HcommSimtCompletionState* HcommSimtCompletionHeader(const UbcCtpCompletionSet&);
HcommSimtCompletionChannel* HcommSimtCompletionChannels(const UbcCtpCompletionSet&);
HcommSimtCompletionCq* HcommSimtCompletionCqs(const UbcCtpCompletionSet&);
template<int,typename G> struct Hcomm {
int32_t WriteNbi(UbcCtpBatchHandle&,uint8_t*,uint8_t*,uint32_t);
int32_t ReadNbi(UbcCtpBatchHandle&,uint8_t*,uint8_t*,uint32_t);
int32_t WriteWithNotifyNbi(UbcCtpBatchHandle&,uint8_t*,uint8_t*,uint32_t,uint8_t*,uint64_t);
Hcomm(G); int32_t Init(void*,uint32_t);
UbcCtpBatchHandle MakeBatchHandle(uint64_t,uint8_t*,uint32_t,uint8_t*,uint32_t);
int32_t Drain(UbcCtpBatchHandle&); int32_t Drain(UbcCtpCompletionSet&);
int32_t InitCompletionSet(UbcCtpCompletionSet&,uint64_t*,uint32_t,uint8_t*,uint32_t);
int32_t PrepareCompletion(UbcCtpCompletionSet&,uint32_t,uint32_t,uint32_t);
};
}}
template<auto Fn,typename... Args> void asc_vf_call(dim3,Args... args) { Fn(args...); }
'''

example=root/'examples/multi_jetty_batch_perf'
def compile_source(source,batch):
    return subprocess.run(['g++','-std=c++17','-fsyntax-only','-x','c++',
                           '-I'+str(example),'-DMULTI_JETTY_MATRIX_BATCH='+str(batch),
                           '-DMULTI_JETTY_HCOMM_SIMT_HEADER=1','-'],input=source,text=True,capture_output=True)
# Only SDK includes and the device launch spelling are substituted; all example bodies remain.
source=re.sub(r'^#include ("kernel_operator.h"|"simt_api/[^"]+"|MULTI_JETTY_HCOMM_SIMT_HEADER)\n','',s,flags=re.M)
source=source.replace('<<<1U, 0U, stream>>>','')
for batch in (1,8,32,64,128,256):
    result=compile_source(prefix+source,batch)
    assert result.returncode==0,result.stderr
print('PASS: complete current device source and launch argument types, six specializations (SDK declarations substituted)')
broken=source.replace('statuses[firstJetty]','statuses[undeclaredJetty]')
result=compile_source(prefix+broken,1)
assert result.returncode!=0 and 'undeclaredJetty' in result.stderr
# Compile the complete host file too, including actual setup/validation helpers.
host_stub=r'''
#include <cstdint>
#include <cstddef>
using aclError=int; using aclrtStream=void*; using HcclResult=int; using HcclComm=void*;
using HcclMemHandle=void*; using ChannelHandle=uint64_t; using CommProtocol=int;
constexpr int ACL_SUCCESS=0,ACL_MEM_MALLOC_HUGE_FIRST=0,ACL_MEMCPY_HOST_TO_DEVICE=0,ACL_MEMCPY_DEVICE_TO_HOST=1;
constexpr int HCCL_SUCCESS=0,HCCL_E_SYSCALL=1,HCCL_E_TIMEOUT=2,HCCL_E_INTERNAL=3,HCCL_E_PARA=4,HCCL_E_NOT_SUPPORT=5;
constexpr int COMM_PROTOCOL_UBC_CTP=0,COMM_ENGINE_AIV=0,COMM_MEM_TYPE_DEVICE=0;
struct HcclRootInfo { char data[4096]; };
struct CommMem { int type; void* addr; size_t size; };
struct HcclChannelDesc { uint32_t remoteRank,notifyNum,memHandleNum; int channelProtocol,localEndpoint,remoteEndpoint; HcclMemHandle* memHandles; };
struct CommLink { struct { int linkProtocol; } linkAttr; int srcEndpointDesc,dstEndpointDesc; };
'''
functions=('aclrtMemcpy aclInit aclrtSetDevice aclrtCreateStream aclrtMalloc aclrtMemset aclrtSynchronizeStream '
           'aclrtFree aclrtDestroyStream aclFinalize HcclGetRootInfo HcclCommInitRootInfo HcclChannelDescInit '
           'HcclRankGraphGetLayers HcclRankGraphGetLinks HcclChannelGetRemoteMems HcclChannelAcquire HcclCommMemReg HcclCommDestroy')
host_stub+='\n'.join('int '+name+'(...);' for name in functions.split())
setup=(example/'host_setup.h').read_text()
setup=re.sub(r'^#include "(acl|hccl)/[^"]+"\n','',setup,flags=re.M)
main=(example/'main.cpp').read_text().replace('#include "host_setup.h"',setup)
result=compile_source(host_stub+main,256)
assert result.returncode==0,result.stderr
print('PASS: complete host source (ACL/HCCL declarations substituted); negative undeclared-identifier check')
