#include "gtest/gtest.h"
#include <mockcpp/mockcpp.hpp>

#include "src/base_comm/resources/ccu/ccu_device/ccu_res_batch_allocator.h"
#include "ccu_comp.h"
#include "hccl_common.h"

#define private public
#define protected public

using namespace hcomm;

static void InitTestMissionReq(MissionReq& missionReq, MissionReqType reqType)
{
    missionReq.reqType = reqType;
    missionReq.req[0] = 0;
    missionReq.req[1] = 0;
}

class CcuResBatchAllocatorTest : public testing::Test {
protected:
    static void SetUpTestCase() {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }
    static void TearDownTestCase() {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }
    void SetUp() override {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }
    void TearDown() override {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }

};

TEST_F(CcuResBatchAllocatorTest, Ut_Alloc_When_ReqTypeNotDefault_Expect_ForceSetToDefault)
{   
    hcomm::CcuResBatchAllocator allocat{};
    uintptr_t handleKey;
    MissionReq missionReq;
    MissionResInfo missionInfos;

    // 调用公共函数
    InitTestMissionReq(missionReq, MissionReqType::COMM_ENGINE_RESERVED);

    auto ret = allocat.missionMgr_.Alloc(handleKey, missionReq, missionInfos);
    EXPECT_EQ(ret, HcclResult::HCCL_SUCCESS);
}

TEST_F(CcuResBatchAllocatorTest, Ut_Alloc_When_ReqNumIs0_Expect_ReturnSuccessDirectly)
{
    hcomm::CcuResBatchAllocator allocat{};
    uintptr_t handleKey;
    MissionReq missionReq;
    MissionResInfo missionInfos;

    // 调用公共函数
    InitTestMissionReq(missionReq, MissionReqType::FUSION_MULTIPLE_DIE);

    auto ret = allocat.missionMgr_.Alloc(handleKey, missionReq, missionInfos);
    EXPECT_EQ(ret, HcclResult::HCCL_SUCCESS);
}