/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "../../../src/aicpu/asccomm/resource/connection/ub_queue_budget.h"
#include "../../../src/aicpu/asccomm/resource/connection/ub_completion.h"
#include <cassert>
int main()
{
    AsccommUbCompletion completion;
    completion.generation = 1;
    AsccommUbCompletionTag old{&completion, 1};
    ++completion.generation;
    AsccommUbCompletionTag current{&completion, 2};
    old.Complete();
    old.Fail();
    assert(!completion.done && !completion.failed);
    current.Complete();
    assert(completion.done);
    Asc::UbQueueBudget b;
    b.depth = 8;
    assert(!b.Reserve(65534, 1));
    b.Completed(65534);
    assert(b.Reserve(65534, 2));
    assert(b.expectedPi == 0 && b.ci == 65534);
    assert(b.Reserve(0, 6));
    assert(!b.Reserve(6, 1));
    assert(!b.HasSpace(9));
    assert(b.ci == 65534); // enqueue is not completion
    b.Completed(6);
    assert(b.Reserve(6, 2));
    assert(!b.Reserve(7, 1)); // unexpected external writer
    b.failed = true;
    assert(!b.Reserve(8, 1));

    Asc::SharedUbQueueBudget shared;
    shared.depth = 8;
    assert(shared.Refresh(4, 2)); // HCCL first: used queue is legal
    assert(shared.used == 2 && shared.Reserve(4, 3));
    assert(shared.Refresh(9, 5)); // HCCL advanced PI outside asccomm
    assert(shared.expectedPi == 9 && shared.used == 4);
    assert(!shared.Reserve(9, 5)); // external publication is not completion
    assert(shared.Reserve(9, 4));
    shared.Completed(13);          // actual local completion proof
    assert(shared.Refresh(13, 5)); // shared CI may still lag
    assert(shared.used == 0 && shared.ci == 13);
    assert(shared.Reserve(13, 4));
    assert(shared.Refresh(17, 15)); // partial shared completion
    assert(shared.used == 2 && shared.ci == 15);
    assert(shared.Refresh(17, 18)); // CI beyond PI is not accepted
    assert(shared.used == 2 && shared.ci == 15);

    Asc::SharedUbQueueBudget wrap;
    wrap.depth = 8;
    assert(wrap.Refresh(1, 65533));
    assert(wrap.used == 4 && wrap.Reserve(1, 4));
    assert(wrap.Refresh(7, 5));
    assert(wrap.used == 2);
    assert(wrap.Refresh(40, 5)); // ambiguous/stale CI: never invent free space
    assert(wrap.needsCompletion && !wrap.HasSpace(1));
    assert(wrap.Refresh(40, 40)); // ambiguous history needs an explicit proof
    assert(wrap.needsCompletion && !wrap.HasSpace(1));
    wrap.Completed(40);
    assert(!wrap.needsCompletion && wrap.HasSpace(8));
    wrap.failed = true;
    assert(!wrap.Refresh(41, 41)); // cannot hide an earlier publication failure
    Asc::SharedUbQueueBudget invalid;
    assert(!invalid.Refresh(0, 0));
    invalid.depth = 65536;
    assert(!invalid.Refresh(0, 0));
}
