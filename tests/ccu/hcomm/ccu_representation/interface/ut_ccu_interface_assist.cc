#include "gtest/gtest.h"
#include <mockcpp/mockcpp.hpp>


#include "ccu_kernel.h"
#include "exception_util.h"
#include "ccu_api_exception.h"

using namespace hcomm;

class AppendToContextTest : public testing::Test {
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

TEST_F(AppendToContextTest, Ut_AppendToContext_When_NullContext_Expect_ShouldThrowException)
{
    std::shared_ptr<CcuRep::CcuRepBase> rep = std::make_shared<CcuRep::CcuRepBlock>("test_rep");
    
    EXPECT_THROW(
        AppendToContext(nullptr,rep),
        Hccl::CcuApiException
    );
}