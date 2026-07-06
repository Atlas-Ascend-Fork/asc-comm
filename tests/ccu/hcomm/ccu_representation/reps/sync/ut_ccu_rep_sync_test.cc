/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <climits>

#include "ccu_rep_loc_wait_event.h"
#include "ccu_rep_loc_wait_notify.h"
#include "ccu_rep_loc_record_event.h"
#include "ccu_rep_record_shared_notify.h"
#include "ccu_rep_rempostsem_v1.h"
#include "ccu_rep_rempostvar_v1.h"
#include "ccu_rep_remwaitsem_v1.h"
#include "ccu_datatype_v1.h"
#include "ccu_rep_context_v1.h"

namespace hcomm {
namespace CcuRep {
namespace {

class CcuRepLocWaitEventTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

class CcuRepLocWaitNotifyTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

class CcuRepLocRecordEventTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

class CcuRepRecordSharedNotifyTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

class CcuRepRemPostSemTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

class CcuRepRemPostVarTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

class CcuRepRemWaitSemTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(CcuRepLocWaitEventTest, ConstructorInitializesCorrectly)
{
    CompletedEvent event;
    CcuRepLocWaitEvent rep(event, 0xF, true);

    EXPECT_EQ(rep.Type(), CcuRepType::LOC_WAIT_EVENT);
    EXPECT_EQ(rep.InstrCount(), 1);
    EXPECT_EQ(rep.GetMask(), 0xF);
}

TEST_F(CcuRepLocWaitEventTest, ConstructorWithProfilingDisabled)
{
    CompletedEvent event;
    CcuRepLocWaitEvent rep(event, 0xA, false);

    EXPECT_EQ(rep.Type(), CcuRepType::LOC_WAIT_EVENT);
    EXPECT_EQ(rep.InstrCount(), 1);
    EXPECT_FALSE(rep.Translated());
}

TEST_F(CcuRepLocWaitEventTest, TranslateWithProfilingEnabled)
{
    CompletedEvent event;
    CcuRepLocWaitEvent rep(event, 0xF, true);

    CcuInstr instr;
    CcuInstr* instrPtr = &instr;
    uint16_t instrId = 0;
    TransDep dep = {};

    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(rep.StartInstrId(), 0);
    EXPECT_EQ(instrId, 1);
    EXPECT_EQ(instr.header.type, 0x1);
    EXPECT_EQ(instr.header.code, 0x2);
}

TEST_F(CcuRepLocWaitEventTest, TranslateWithProfilingDisabled)
{
    CompletedEvent event;
    CcuRepLocWaitEvent rep(event, 0x5, false);

    CcuInstr instr;
    CcuInstr* instrPtr = &instr;
    uint16_t instrId = 10;
    TransDep dep = {};

    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(instrId, 11);
    EXPECT_EQ(instr.header.type, 0x1);
    EXPECT_EQ(instr.header.code, 0x4);
}

TEST_F(CcuRepLocWaitEventTest, Describe)
{
    CompletedEvent event;
    CcuRepLocWaitEvent rep(event, 0xABCD, true);

    std::string desc = rep.Describe();

    EXPECT_NE(desc.find("CcuRepLocWaitEvent"), std::string::npos);
    EXPECT_NE(desc.find("mask"), std::string::npos);
}

TEST_F(CcuRepLocWaitNotifyTest, ConstructorInitializesCorrectly)
{
    LocalNotify notify;
    uint32_t mask = 0xFF;
    CcuRepLocWaitNotify rep(notify, mask, true);

    EXPECT_EQ(rep.Type(), CcuRepType::LOC_WAIT_EVENT);
    EXPECT_EQ(rep.InstrCount(), 1);
    EXPECT_EQ(rep.GetMask(), 0xFF);
}

TEST_F(CcuRepLocWaitNotifyTest, ConstructorWithProfilingDisabled)
{
    LocalNotify notify;
    uint32_t mask = 0x0F;
    CcuRepLocWaitNotify rep(notify, mask, false);

    EXPECT_EQ(rep.Type(), CcuRepType::LOC_WAIT_EVENT);
    EXPECT_FALSE(rep.Translated());
}

TEST_F(CcuRepLocWaitNotifyTest, TranslateWithProfilingEnabled)
{
    LocalNotify notify;
    uint32_t mask = 0xF;
    CcuRepLocWaitNotify rep(notify, mask, true);

    CcuInstr instr;
    CcuInstr* instrPtr = &instr;
    uint16_t instrId = 0;
    TransDep dep = {};

    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(instrId, 1);
    EXPECT_EQ(instr.header.type, 0x1);
    EXPECT_EQ(instr.header.code, 0x2);
}

TEST_F(CcuRepLocWaitNotifyTest, TranslateWithProfilingDisabled)
{
    LocalNotify notify;
    uint32_t mask = 0xA;
    CcuRepLocWaitNotify rep(notify, mask, false);

    CcuInstr instr;
    CcuInstr* instrPtr = &instr;
    uint16_t instrId = 5;
    TransDep dep = {};

    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(instrId, 6);
    EXPECT_EQ(instr.header.type, 0x1);
    EXPECT_EQ(instr.header.code, 0x4);
}

TEST_F(CcuRepLocWaitNotifyTest, Describe)
{
    LocalNotify notify;
    uint32_t mask = 0x1234;
    CcuRepLocWaitNotify rep(notify, mask, true);

    std::string desc = rep.Describe();

    EXPECT_NE(desc.find("CcuRepLocWaitNotify"), std::string::npos);
}

TEST_F(CcuRepLocRecordEventTest, ConstructorInitializesCorrectly)
{
    CompletedEvent event;
    CcuRepLocRecordEvent rep(event, 0xF);

    EXPECT_EQ(rep.Type(), CcuRepType::LOC_RECORD_EVENT);
    EXPECT_EQ(rep.InstrCount(), 1);
    EXPECT_EQ(rep.GetMask(), 0xF);
}

TEST_F(CcuRepLocRecordEventTest, Translate)
{
    CompletedEvent event;
    CcuRepLocRecordEvent rep(event, 0xABCD);

    CcuInstr instr;
    CcuInstr* instrPtr = &instr;
    uint16_t instrId = 100;
    TransDep dep = {};

    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(rep.StartInstrId(), 100);
    EXPECT_EQ(instrId, 101);
    EXPECT_EQ(instr.header.type, 0x1);
    EXPECT_EQ(instr.header.code, 0x2);
}

TEST_F(CcuRepLocRecordEventTest, Describe)
{
    CompletedEvent event;
    CcuRepLocRecordEvent rep(event, 0xABCD);

    std::string desc = rep.Describe();

    EXPECT_NE(desc.find("CcuRepLocRecordEvent"), std::string::npos);
    EXPECT_NE(desc.find("mask"), std::string::npos);
}

TEST_F(CcuRepRecordSharedNotifyTest, ConstructorInitializesCorrectly)
{
    LocalNotify notify;
    uint16_t mask = 0xFF;
    CcuRepRecordSharedNotify rep(notify, mask);

    EXPECT_EQ(rep.Type(), CcuRepType::RECORD_SHARED_NOTIFY);
    EXPECT_EQ(rep.InstrCount(), 1);
    EXPECT_EQ(rep.GetMask(), 0xFF);
}

TEST_F(CcuRepRecordSharedNotifyTest, TranslateSameDie)
{
    LocalNotify notify;
    uint16_t mask = 0xF;
    CcuRepRecordSharedNotify rep(notify, mask);

    CcuInstr instr;
    CcuInstr* instrPtr = &instr;
    uint16_t instrId = 0;
    TransDep dep = {};
    dep.dieId = 0;

    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(instrId, 1);
    EXPECT_EQ(instr.header.type, 0x1);
    EXPECT_EQ(instr.header.code, 0x2);
}

TEST_F(CcuRepRecordSharedNotifyTest, TranslateDifferentDie)
{
    LocalNotify notify;
    uint16_t mask = 0xA;
    CcuRepRecordSharedNotify rep(notify, mask);

    CcuInstr instr;
    CcuInstr* instrPtr = &instr;
    uint16_t instrId = 5;
    TransDep dep = {};
    dep.dieId = 1;
    dep.reserveCkeId = 2;
    dep.reserveChannalId[1] = 3;

    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(instrId, 6);
    EXPECT_EQ(instr.header.type, 0x2);
    EXPECT_EQ(instr.header.code, 0xb);
}

TEST_F(CcuRepRecordSharedNotifyTest, Describe)
{
    LocalNotify notify;
    uint16_t mask = 0x1234;
    CcuRepRecordSharedNotify rep(notify, mask);

    std::string desc = rep.Describe();

    EXPECT_NE(desc.find("Post"), std::string::npos);
    EXPECT_NE(desc.find("mask"), std::string::npos);
}

}
}
}