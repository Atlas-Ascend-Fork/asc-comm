#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
"""Compile the actual shared-CQ implementation against host memory/intrinsic stubs.

Run locally with Python 3 and g++. No CANN/NPU or target helper files are needed.
This checks accounting/ownership, not hardware cache or DMA semantics.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
inner = (ROOT / "src/aicore/hcomm/common/hcomm_simt_inner_def.h").read_text(encoding="utf-8")
types = inner[inner.index("// Completion state is caller-owned"):inner.index("template <typename T>\nstruct ChannelTraits;")]
impl = (ROOT / "src/aicore/hcomm/impl/platform_v310/hcomm_simt_urma.h").read_text(encoding="utf-8")
core = impl[impl.index("// Shared CQ completion implementation."):impl.index("// End shared CQ completion implementation.")]
core = core.replace("HcommImpl<COMM_PROTOCOL_UBC_CTP>::", "CompletionEngine::")
definition = (ROOT / "src/aicore/hcomm/impl/platform_v310/hcomm_simt_urma_def.h").read_text(encoding="utf-8")
api = definition[definition.index("    __simt_callee__ inline int32_t InitCompletionSet("):definition.index("private:\n    bool initialized_")]
example = ROOT / "examples/multi_jetty_batch_perf"
common = (example / "multi_jetty_batch_perf_common.h").read_text(encoding="utf-8")
summary_types = common.split("namespace multi_jetty_batch_perf {", 1)[1].split("} // namespace multi_jetty_batch_perf")[0]
validation = (example / "validation.h").read_text(encoding="utf-8")
identity_check = validation.split("namespace multi_jetty_batch_perf {", 1)[1].split("} // namespace multi_jetty_batch_perf")[0]
completion_helpers = (example / "simt_completion.h").read_text(encoding="utf-8").split("namespace multi_jetty_batch_perf {", 1)[1].split("} // namespace multi_jetty_batch_perf")[0]
completion_helpers = completion_helpers.replace("AscendC::simt::", "").replace("AscendC::", "")
prefix = r"""
#include <array>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <vector>
#define __simt_callee__
#define __gm__
#define __ubuf__
using ChannelHandle = uint64_t;
constexpr int HCOMM_SUCCESS=0, HCOMM_FAILED=-1;
constexpr uint32_t HCOMM_URMA_DEFAULT_QP_IDX=0;
constexpr int SQ_CONTEXT_TYPE_UB_JFS=0, CQ_CONTEXT_TYPE_UB_JFC=0;
void asc_threadfence() {}
void asc_dcci_single(void*) {}
// Only metadata fields accessed by the extracted production code are stubbed.
// The production ABI/layout is not replaced or modified by this test.
struct SqContext { int type=0; struct { struct {
    uint64_t sqVa=0, headAddr=0, tailAddr=0; uint32_t jfsID=0, sqDepth=0;
} ubJfs; } contextInfo; };
struct CqContext { int type=0; struct { struct {
    uint64_t scqVa=0, dbVa=0, tailAddr=0; uint32_t cqDepth=0, cqeSize=0, jfcID=0;
} ubJfc; } contextInfo; };
struct HcommSimtChannelEntity { uint32_t sqNum=1,cqNum=1; uint64_t sqContextAddr=0,cqContextAddr=0; };
"""
harness = r"""
struct Fixture {
    CompletionEngine engine;
    UbcCtpCompletionSet set;
    alignas(128) uint8_t audit[kCompletionAllocationBytes]{};
    uint8_t* workspace=audit+kCompletionWorkspaceOffset;
    uint32_t activeChannels;
    alignas(128) uint64_t heads[8]{};
    uint32_t tails[8]{}, cqTails[8]{}, dbs[8]{}, producers[8]{};
    uint8_t sqStorage[8][128]{};
    SqContext sqs[8]; CqContext contexts[8]; HcommSimtChannelEntity channels[8];
    uint64_t handles[8];
    std::vector<std::array<uint32_t,16>> queues[8];
    uint32_t ids[8]={0x10001,0x20001,0x30001,0x40001,0x50001,0x60001,0x70001,0x80001};
    uint32_t mapping[8]={};
    uint32_t depth;
    Fixture(uint32_t d=32768, bool multiple=false, uint32_t n=4):activeChannels(n),depth(d) {
        if(multiple) mapping[1]=1;
        for(auto& q:queues) q.resize(depth);
        for(uint32_t i=0;i<activeChannels;++i) {
            auto& sq=sqs[i].contextInfo.ubJfs;
            sq.sqVa=(uint64_t)sqStorage[i];sq.headAddr=(uint64_t)&heads[i];sq.tailAddr=(uint64_t)&tails[i];
            sq.jfsID=ids[i];sq.sqDepth=depth;
            auto& cq=contexts[i].contextInfo.ubJfc;
            auto q=mapping[i];cq.scqVa=(uint64_t)queues[q].data();cq.dbVa=(uint64_t)&dbs[q];
            cq.tailAddr=(uint64_t)&cqTails[i];cq.cqDepth=depth;cq.cqeSize=64;cq.jfcID=42+q;
            channels[i].sqContextAddr=(uint64_t)&sqs[i];channels[i].cqContextAddr=(uint64_t)&contexts[i];
            handles[i]=(uint64_t)&channels[i];
        }
    }
    int init(){return engine.InitCompletionSet(set,handles,activeChannels,workspace,4096);}
    auto* entries(){return HcommSimtCompletionChannels(set);}
    auto* cqs(){return HcommSimtCompletionCqs(set);}
    auto* state(){return HcommSimtCompletionHeader(set);}
    void admit(uint32_t j,uint32_t n,uint32_t bb) {
        assert(engine.PrepareCompletion(set,j,n,bb)==0);
        heads[j]=((uint64_t)entries()[j].target<<32)|entries()[j].targetBb;
    }
    void raw(uint32_t q,uint32_t id,uint32_t status=0,uint32_t entry=0) {
        auto seq=producers[q]++;
        auto& w=queues[q][seq%depth];w.fill(0);
        w[0]=2U|((1U-((seq/depth)&1U))<<2U)|(status<<24U);
        w[1]=((id&0xffff)<<16U)|(entry&0xffff);w[2]=id>>16U;
    }
    void emit(uint32_t j,uint32_t status=0,uint32_t entry=0){raw(mapping[j],ids[j],status,entry);}
};
int main() {
    { // Reproduce V2's false-completion prefix with four different full IDs sharing low16.
        Fixture f;assert(f.init()==0);
        for(unsigned j=0;j<4;++j) f.admit(j,1280,1285);
        unsigned counts[4]={320,304,336,320};
        for(unsigned j=0;j<4;++j)for(unsigned n=0;n<counts[j];++n) f.emit(j,0,n);
        uint32_t n=0;assert(f.engine.Progress(f.set,1280,n)==0 && n==1280);
        for(unsigned j=0;j<4;++j){assert(f.entries()[j].completed==counts[j]);assert(f.tails[j]==0);}
        assert(f.dbs[0]==1280);
        assert(f.engine.WaitCompletion(f.set,0,2)==-1);
        assert(f.state()->fault==5 && f.state()->errorChannel==0);
    }
    { // All 72 cases use the actual window setup, completion owner and snapshot/check helpers.
        for(uint32_t api=0;api<3;++api) for(uint32_t bs:{1U,8U,32U,64U,128U,256U})
        for(uint32_t n:{1U,2U,4U,8U}) {
            Fixture f(32768,false,n);assert(f.init()==0);
            const uint32_t warm=(96U+bs-1U)/bs*bs, operations=warm+1024U;
            const uint32_t perKernel=1024U/bs;
            auto* summaries=reinterpret_cast<CompletionSummary*>(f.audit);
            int32_t statuses[8]{};
            for(uint32_t first=0;first<n;first+=perKernel) {
                const uint32_t count=std::min(n-first,perKernel);
                // A later launch resumes this state; it must never reinitialize shared CQ cursors.
                auto resumed=CompletionSet(f.audit);
                assert(resumed.workspace==f.set.workspace);
                if(first) assert(ResumeCompletionSet(resumed,first)==0);
                for(uint32_t phase=0;phase<2;++phase) {
                    uint32_t amount=phase?1024U:warm;
                    assert(PrepareCompletionWindows(f.engine,resumed,count,amount,bs,api,first)==0);
                    for(uint32_t j=first;j<first+count;++j)
                        f.heads[j]=((uint64_t)f.entries()[j].target<<32)|f.entries()[j].targetBb;
                    for(uint32_t i=0;i<amount;++i)for(uint32_t j=first;j<first+count;++j)f.emit(j);
                    assert(f.engine.Drain(resumed)==0);
                    if(!phase) CaptureWarmupCompletion(resumed,summaries,count,first);
                }
                CaptureCompletion(f.handles,summaries,count,2,statuses,first);
            }
            std::vector<CompletionSummary> copy(summaries,summaries+n);
            assert(CheckCompletion(copy,warm,operations,bs,api,perKernel));
            assert(f.dbs[0]==operations*n);
            // Reject private-cursor reset and Write BB arithmetic incorrectly used for Notify.
            if(n==8 && bs==256) {
                copy[4].warmupCq=4*warm;
                assert(!CheckCompletion(copy,warm,operations,bs,api,perKernel));
            }
            if(api==2) {
                copy.assign(summaries,summaries+n);copy[0].sqTail=operations+operations/bs;
                if(bs!=1) assert(!CheckCompletion(copy,warm,operations,bs,api,perKernel));
            }
        }
    }
    { // A failed/incomplete earlier launch must not be retried by a later kernel.
        Fixture f(32768,false,8);assert(f.init()==0);f.admit(0,1,2);
        assert(ResumeCompletionSet(f.set,4)==-1);
        f.emit(0);assert(f.engine.Drain(f.set)==0);assert(ResumeCompletionSet(f.set,4)==0);
        f.state()->fault=1;assert(ResumeCompletionSet(f.set,4)==-1);
    }
    { // Read checks remote identity; Notify checks every flag as well as every data byte.
        for(uint32_t api=0;api<3;++api) {
            uint32_t slot=api==2?72:64;std::vector<uint8_t> data(2*slot);
            for(uint32_t op=0;op<2;++op) {
                for(uint32_t b=0;b<64;++b)data[op*slot+b]=IdentityByte(api==1,0,op,b);
                if(api==2){auto value=NotifyValue(0,op);std::memcpy(data.data()+op*slot+64,&value,8);}
            }
            assert(CheckIdentityBuffer(data,api==1,1,2,64,slot,2*slot,1,api));
            if(api==2)data[slot+64]^=1;else data[slot+63]^=1;
            assert(!CheckIdentityBuffer(data,api==1,1,2,64,slot,2*slot,1,api));
        }
    }
    { // Actual warmup/timed geometry, cumulative CQ accounting, and idempotent Drain.
        Fixture f;assert(f.init()==0);
        for(unsigned phase=0;phase<2;++phase) {
            auto n=phase?1024:256;auto bb=phase?1028:257;
            for(unsigned j=0;j<4;++j) f.admit(j,n,bb);
            for(int i=0;i<n;++i)for(unsigned j=0;j<4;++j)f.emit((j+2)%4,0,i);
            assert(f.engine.Drain(f.set)==0);
            for(unsigned j=0;j<4;++j){assert(f.entries()[j].completed==(phase?1280:256));assert(f.tails[j]==(phase?1285:257));}
            assert(f.dbs[0]==(phase?5120:1024));
        }
        assert(f.engine.Drain(f.set)==0);assert(f.dbs[0]==5120);
    }
    { // Slow J0 must not prevent J1 waiting, reclaiming or admitting a second window.
        Fixture f;assert(f.init()==0);f.admit(0,16,17);f.admit(1,2,3);
        f.emit(1);uint32_t n=0;assert(f.engine.Progress(f.set,1,n)==0 && n==1);
        assert(f.tails[1]==0);f.emit(1);assert(f.engine.WaitCompletion(f.set,1,2)==0);
        assert(f.tails[1]==3 && f.tails[0]==0 && f.entries()[0].completed==0);
        f.admit(1,3,4);for(unsigned i=0;i<3;++i)f.emit(1);
        assert(f.engine.WaitCompletion(f.set,1,2)==0);assert(f.tails[1]==7 && f.tails[0]==0);
    }
    { // An empty CQ does not block a ready independent CQ.
        Fixture f(16,true);assert(f.init()==0);f.admit(0,3,4);f.admit(1,2,3);
        f.emit(1);f.emit(1);assert(f.engine.WaitCompletion(f.set,1,2)==0);
        assert(f.dbs[1]==2 && f.dbs[0]==0 && f.tails[0]==0);
    }
    { // A budget boundary must preserve round-robin position across independent CQs.
        Fixture f(16,true);assert(f.init()==0);f.admit(0,2,3);f.admit(1,2,3);
        f.emit(0);f.emit(1);uint32_t n=0;
        assert(f.engine.Progress(f.set,1,n)==0 && n==1);
        assert(f.state()->nextCq==1 && f.entries()[0].completed==1 && f.dbs[0]==1);
        assert(f.engine.Progress(f.set,1,n)==0 && n==1);
        assert(f.state()->nextCq==0 && f.entries()[1].completed==1 && f.dbs[1]==1);
    }
    { // Shared CQ admission is aggregate; failed admission has no state/queue mutation.
        Fixture f(8);assert(f.init()==0);f.admit(0,5,6);
        assert(f.engine.PrepareCompletion(f.set,1,3,4)==-1);
        assert(f.entries()[1].target==0 && f.heads[1]==0 && f.state()->fault==0);
        assert(f.engine.PrepareCompletion(f.set,0,1,1)==-1);
        f.emit(0);f.emit(0);uint32_t n;assert(f.engine.Progress(f.set,2,n)==0);
        f.admit(1,3,4);assert(f.tails[0]==0);
    }
    { // Owner-bit and SQ wrap across six independently completed windows.
        Fixture f(8);assert(f.init()==0);
        for(unsigned r=0;r<6;++r){f.admit(0,3,3);for(unsigned i=0;i<3;++i)f.emit(0,0,(r*3+i)%8);
            assert(f.engine.WaitCompletion(f.set,0,2)==0);assert(f.tails[0]==3*(r+1));}
        assert(f.cqs()[0].consumed==18 && f.dbs[0]==18);
    }
    { // Actual error owner and only the preceding valid prefix may be acknowledged.
        Fixture f;assert(f.init()==0);f.admit(0,4,5);f.admit(2,2,3);
        f.emit(0);f.emit(0);f.emit(2,6,192);uint32_t n;
        assert(f.engine.Progress(f.set,64,n)==-1 && n==2);
        assert(f.state()->fault==3 && f.state()->errorChannel==2 && f.state()->errorSequence==2);
        assert(f.state()->errorWord0==0x06000006 && f.dbs[0]==2);
        assert(f.entries()[2].completed==0 && f.tails[0]==0 && f.tails[2]==0);
        assert(f.engine.Drain(f.set)==-1);
    }
    { // Unknown high ID bits must never be matched using only low16.
        Fixture f;assert(f.init()==0);f.admit(0,2,3);f.emit(0);f.raw(0,0xf0001);
        uint32_t n;assert(f.engine.Progress(f.set,64,n)==-1 && n==1);
        assert(f.state()->fault==2 && f.state()->errorChannel==0xffffffff && f.dbs[0]==1);
    }
    { // Extra completion is not counted against a future unadmitted window.
        Fixture f;assert(f.init()==0);f.admit(0,2,3);f.emit(0);f.emit(0);f.emit(0);
        uint32_t n;assert(f.engine.Progress(f.set,64,n)==-1 && n==2);
        assert(f.state()->fault==4 && f.entries()[0].completed==2 && f.dbs[0]==2);
    }
    { // Invalid BB endpoint must not free SQ space, even if the target CQE arrives.
        Fixture f;assert(f.init()==0);f.admit(0,2,3);++f.heads[0];f.emit(0);f.emit(0);
        uint32_t n;assert(f.engine.Progress(f.set,64,n)==-1 && n==1);
        assert(f.state()->fault==4 && f.tails[0]==0 && f.dbs[0]==1);
    }
    { Fixture f;f.sqs[1].contextInfo.ubJfs.jfsID=f.ids[0];assert(f.init()==-1); }
    { Fixture f;f.sqs[1].contextInfo.ubJfs.sqVa=f.sqs[0].contextInfo.ubJfs.sqVa;assert(f.init()==-1); }
    { Fixture f;f.heads[0]=1;assert(f.init()==-1); }
    { Fixture f;assert(f.init()==0);assert(f.init()==-1); }
    { Fixture f;assert(f.init()==0);uint32_t n=99;assert(f.engine.Progress(f.set,64,n)==0 && n==0);
      assert(f.engine.PrepareCompletion(f.set,0,0,0)==-1); }
    { // The host gate must reject a nominal PASS with the original mixed completion counts.
        std::vector<CompletionSummary> records(4);
        for(auto& r:records){r.magic=kCompletionSummaryMagic;r.completionVersion=1;r.phase=2;
            r.warmupCompleted=256;r.warmupCq=1024;r.warmupSq=257;
            r.completed=1280;r.target=1280;r.targetBb=1285;r.sqTail=1285;
            r.publishedHead=(1280ULL<<32U)|1285;r.cqTail=5120;r.acknowledged=5120;}
        assert(CheckCompletion(records,256,1280,256));
        records[1].completed=304;assert(!CheckCompletion(records,256,1280,256));
        records[1].completed=1280;records[0].acknowledged=1280;
        assert(!CheckCompletion(records,256,1280,256));
    }
    std::cout << "PASS: shared CQ identity, skew, independent windows, capacity, wrap, faults, and 5120 completions\n";
}
"""
with tempfile.TemporaryDirectory(prefix="simt-completion-") as tmp:
    source = Path(tmp) / "completion.cpp"
    executable = Path(tmp) / "completion"
    source.write_text(prefix + types + summary_types + "\nstruct CompletionEngine {\n" + api + "};\n" +
        core + completion_helpers + identity_check + harness, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", str(source), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    print("PASS: shared CQ ownership, capacity, wrap, faults, 72 matrix geometries and Read/Notify validation")
