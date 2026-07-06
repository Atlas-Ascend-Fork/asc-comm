#include "gtest/gtest.h"
#include <mockcpp/mockcpp.hpp>

#include "ccu_comp.h"
#include "hccl_common.h"

#include "unified_platform/ccu/ccu_device/ccu_component/ccu_component.h"

#include "ccu_pfe_cfg_mgr.h"


#define private public
#define protected public
using namespace hcomm;


class CcuPfeCfgMgrTest : public testing::Test {
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

TEST_F(CcuPfeCfgMgrTest, Ut_GetPfeJettyCtxCfg_When_DieIdInvalid_Expect_ReturnEmpty) {
 	auto& mgr = CcuPfeCfgMgr::GetInstance(0);
 	     // 传入无效 dieId，验证返回空向量以覆盖 warning 分支
 	EXPECT_TRUE(mgr.GetPfeJettyCtxCfg(CCU_MAX_IODIE_NUM).empty());
}