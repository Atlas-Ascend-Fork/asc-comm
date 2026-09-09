/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_CONTROL_FLOW_MACRO_H
#define CCU_CONTROL_FLOW_MACRO_H

#include "ccu/hcomm/ccu_variable.hpp"
#include "ccu/hcomm/ccu_primitives_impl.h"

#define CCU_CONCAT_INNER(a, b) a##b

#define CCU_CONCAT(a, b) CCU_CONCAT_INNER(a, b)

#define CCU_STRINGIFY_INNER(x) #x

#define CCU_STRINGIFY(x) CCU_STRINGIFY_INNER(x)

#define CCU_LABEL(uid) (__FILE__ ":" CCU_STRINGIFY(__LINE__) ":" CCU_STRINGIFY(uid))

#define CCU_WHILE(expr) CCU_WHILE_EXPAND(expr, CCU_CONCAT(__ccu_wh_, __COUNTER__))

#define CCU_WHILE_EXPAND(expr, uid) CCU_WHILE_IMPL(expr, uid)

#define CCU_WHILE_IMPL(expr, uid)                                                                                      \
    for (::AscendC::ccu::cond_expr uid##_ce = (expr), *uid##_p = &uid##_ce; uid##_p != nullptr; uid##_p = nullptr)     \
        for (const char *uid##_dwLbl = ::asc::ccu_do_while_stack_pop_for_while(), *uid##_sen = (const char*)1;         \
             uid##_sen != nullptr; uid##_sen = nullptr)                                                                \
            for (int uid##_rc = (uid##_dwLbl != nullptr) ?                                                             \
                                    (int)CCU_SUCCESS :                                                                 \
                                    (int)::asc::ccu_while_begin(                                                       \
                                        uid##_ce.var_->handle, uid##_ce.imm, uid##_ce.cond, CCU_LABEL(uid)),           \
                     uid##_done = 0;                                                                                   \
                 uid##_rc == (int)CCU_SUCCESS && !uid##_done;                                                          \
                 uid##_done = 1, uid##_rc = (uid##_dwLbl != nullptr) ?                                                 \
                                                (int)::asc::ccu_do_while_end(                                          \
                                                    uid##_ce.var_->handle, uid##_ce.imm, uid##_ce.cond, uid##_dwLbl) : \
                                                (int)::asc::ccu_while_end(CCU_LABEL(uid)))

#define CCU_IF(expr) CCU_IF_EXPAND(expr, CCU_CONCAT(__ccu_if_, __COUNTER__))

#define CCU_IF_EXPAND(expr, uid) CCU_IF_IMPL(expr, uid)

#define CCU_IF_IMPL(expr, uid)                                                                                     \
    for (::AscendC::ccu::cond_expr uid##_ce = (expr), *uid##_p = &uid##_ce; uid##_p != nullptr; uid##_p = nullptr) \
        for (int uid##_rc =                                                                                        \
                 (int)::asc::ccu_if_begin(uid##_ce.var_->handle, uid##_ce.imm, uid##_ce.cond, CCU_LABEL(uid)),     \
                 uid##_done = (uid##_rc == (int)CCU_SUCCESS ? (::asc::ccu_if_stack_push(CCU_LABEL(uid)), 0) : 1);  \
             uid##_rc == (int)CCU_SUCCESS && uid##_done == 0;                                                      \
             uid##_done = 1, ((void)::asc::ccu_flush_pending_ifs(), ::asc::ccu_if_stack_mark_body_done(), (void)0))

#define CCU_ELSE CCU_ELSE_EXPAND(CCU_CONCAT(__ccu_el_, __COUNTER__))

#define CCU_ELSE_EXPAND(uid) CCU_ELSE_IMPL(uid)

#define CCU_ELSE_IMPL(uid)                                                                                         \
    for (const char *uid##_lbl = ::asc::ccu_if_stack_pop_for_else(), *uid##_sen = uid##_lbl; uid##_sen != nullptr; \
         uid##_sen = nullptr)                                                                                      \
        for (int uid##_rc = (int)::asc::ccu_if_else(uid##_lbl), uid##_done = 0;                                    \
             uid##_rc == (int)CCU_SUCCESS && !uid##_done;                                                          \
             uid##_done = 1, uid##_rc = (int)::asc::ccu_if_end(uid##_lbl))

#define CCU_DO CCU_DO_EXPAND(CCU_CONCAT(__ccu_dw_, __COUNTER__))

#define CCU_DO_EXPAND(uid) CCU_DO_IMPL(uid)

#define CCU_DO_IMPL(uid)                                                                \
    for (int uid##_rc = (int)::asc::ccu_do_while_begin(CCU_LABEL(uid)), uid##_done = 0; \
         uid##_rc == (int)CCU_SUCCESS && !uid##_done; uid##_done = 1, ::asc::ccu_do_while_stack_push(CCU_LABEL(uid)))

#endif // CCU_CONTROL_FLOW_MACRO_H
