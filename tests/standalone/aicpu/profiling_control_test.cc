/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <cassert>
#include "profiling_control.h"
static uint32_t features;
static int32_t Query(uint64_t f) { return (features & f) ? 1 : 0; }
int main()
{
    // Exhaust all six-event sequences, including repeated START and STOP.
    for (unsigned seq = 0; seq < 46656; ++seq) {
        Asc::ProfilingControl state;
        bool l0 = false, l1 = false;
        unsigned n = seq;
        for (unsigned i = 0; i < 6; ++i) {
            const unsigned event = n % 6;
            n /= 6;
            MsprofCommandHandle command{};
            command.type = event == 4 ? PROF_COMMANDHANDLE_TYPE_STOP :
                           event == 5 ? PROF_COMMANDHANDLE_TYPE_MAX :
                                        PROF_COMMANDHANDLE_TYPE_START;
            command.profSwitch = ((event & 1) ? ADPROF_TASK_TIME_L0 : 0) | ((event & 2) ? ADPROF_TASK_TIME_L1 : 0);
            // HCOMM DfxDeviceCommandHandle START sets selected bits, STOP clears both.
            if (command.type == PROF_COMMANDHANDLE_TYPE_START) {
                if (command.profSwitch & ADPROF_TASK_TIME_L0) {
                    l0 = true;
                }
                if (command.profSwitch & ADPROF_TASK_TIME_L1) {
                    l1 = true;
                }
            } else if (command.type == PROF_COMMANDHANDLE_TYPE_STOP) {
                l0 = false;
                l1 = false;
            }
            state.Command(command);
            assert(state.Refresh(true, nullptr) == (unsigned(l0) | (unsigned(l1) << 1)));
        }
        assert(state.Refresh(false, nullptr) == 0);
        for (features = 0; features < 32; ++features) {
            const auto expected =
                ((features & ADPROF_TASK_TIME_L0) ? 1 : 0) | ((features & ADPROF_TASK_TIME_L1) ? 2 : 0);
            assert(state.Refresh(false, Query) == unsigned(expected));
        }
    }
}
