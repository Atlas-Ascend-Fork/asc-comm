/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_ADPT_INTERNAL_H
#define ASCCOMM_ADPT_INTERNAL_H

#include "asccomm_res_defs.h"
#include "hcomm_res_entity_defs.h"
#include "asccomm_launch.h"
#include "asccomm_runtime.h"
#include "asccomm_log.h"
#include "asccomm_result.h"

namespace asccomm_adpt {

struct CommContext {
    ThreadEntity* thread;
    ChannelEntity* channel;
};

template <typename Entity>
asccomm_result validate_entity(const Entity* entity, uint32_t version, uint32_t magicWord)
{
    CHK_PTR_NULL(entity);
    CHK_PRT_RET(
        entity->abiHeader.version != version || entity->abiHeader.magicWord != magicWord,
        ASCCOMM_ERROR(
            "[%s] invalid resource ABI header, version[%u], magicWord[0x%x].", __func__, entity->abiHeader.version,
            entity->abiHeader.magicWord),
        ASCCOMM_E_PARA);
    CHK_PRT_RET(
        entity->abiHeader.size < sizeof(Entity),
        ASCCOMM_ERROR(
            "[%s] resource entity size[%u] is less than expected size[%zu].", __func__, entity->abiHeader.size,
            sizeof(Entity)),
        ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

inline asccomm_result get_thread_entity(
    ThreadHandle thread, bool registerThread, bool resolveRtsq, ThreadEntity*& threadEntity)
{
    CHK_PRT_RET(thread == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
    // The public ThreadHandle is the device resource entity address.  The control
    // plane performs the original-handle -> POD conversion before launch.
    threadEntity = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(thread));
    CHK_RET(validate_entity(threadEntity, HCOMM_THREAD_ENTITY_VERSION, HCOMM_THREAD_ENTITY_MAGIC_WORD));
    // A host stream can be exported as a notify-only thread entity. Such an entity
    // has no RTSQ, so only callers that submit work through the thread require it.
    if (resolveRtsq) {
        CHK_PTR_NULL(threadEntity->sqContextAddr);
        CHK_RET(asccomm_resolve_thread_rtsq(threadEntity));
    }
    if (registerThread) {
        asccomm_register_thread(thread);
    }
    return ASCCOMM_SUCCESS;
}

inline asccomm_result get_context(ThreadHandle thread, ChannelHandle channel, bool registerThread, CommContext& context)
{
    CHK_PRT_RET(channel == 0, ASCCOMM_ERROR("[%s] channel handle is 0.", __func__), ASCCOMM_E_PTR);
    CHK_RET(get_thread_entity(thread, registerThread, true, context.thread));
    // ChannelHandle follows the same resource-address ABI as ThreadHandle.
    context.channel = reinterpret_cast<ChannelEntity*>(static_cast<uintptr_t>(channel));
    CHK_RET(validate_entity(context.channel, HCOMM_CHANNEL_ENTITY_VERSION, HCOMM_CHANNEL_ENTITY_MAGIC_WORD));
    return ASCCOMM_SUCCESS;
}

template <typename Operation>
asccomm_result execute_api(Operation operation)
{
    asccomm_result ret = ASCCOMM_SUCCESS;
    EXCEPTION_CATCH(ret = operation(), ret = ASCCOMM_E_INTERNAL);
    return ret;
}

} // namespace asccomm_adpt

#endif // ASCCOMM_ADPT_INTERNAL_H
