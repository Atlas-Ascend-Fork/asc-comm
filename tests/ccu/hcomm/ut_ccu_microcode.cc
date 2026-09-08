/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "gtest/gtest.h"
#include <mockcpp/mockcpp.hpp>
#include <mockcpp/mokc.h>

#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include "log.h"

using namespace asc;
using namespace asc::ccu_rep;

class CcuMicroCodeTest : public testing::Test {
protected:
    static void SetUpTestCase() { std::cout << "CcuMicroCodeTest tests set up." << std::endl; }

    static void TearDownTestCase() { std::cout << "CcuMicroCodeTest tests tear down." << std::endl; }

    virtual void SetUp() { std::cout << "A Test case in CcuMicroCodeTest SetUP" << std::endl; }

    virtual void TearDown()
    {
        GlobalMockObject::verify();
        std::cout << "A Test case in CcuMicroCodeTest TearDown" << std::endl;
    }
};

TEST_F(CcuMicroCodeTest, ParseInstr_UnsupportedInstrHeader)
{
    ccu_instr ccuInstr = {};
    ccuInstr.header.header = 0xFFFF;
    std::string result = parse_instr(&ccuInstr);
    EXPECT_TRUE(result.find("Unsupported instruction with header") != std::string::npos);
    EXPECT_TRUE(result.find("0xffff") != std::string::npos);
}

TEST_F(CcuMicroCodeTest, ParseMSList_CountExceedsMax)
{
    ccu_instr ccuInstr = {};
    uint16_t ms_id[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    add_instr(&ccuInstr, ms_id, 8, 0, 0, 0, 0x1, 0, 0xff, 1, 0);
    ccuInstr.v1.add.count = 7;
    std::string result = parse_instr(&ccuInstr);
    EXPECT_TRUE(result.find("MS[]") != std::string::npos);
}
