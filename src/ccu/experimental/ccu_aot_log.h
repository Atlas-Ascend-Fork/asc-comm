/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * ccu_aot_log.h — 统一分级日志（AGENTS「日志打印规范」）。
 *
 * 格式：`[<component>] [<level>] <message>`，例 `[ccu_aot] [error] rank=1 ...`。
 * 级别（越大越严重）：debug < info < warning < error。
 * 过滤：环境变量 `CCU_AOT_LOG_LEVEL=debug|info|warning|error`，**默认 warning**
 *       （只打 warning/error，info/debug 静默——减噪；需要细节时 CCU_AOT_LOG_LEVEL=info）。
 *
 * adapter 用 `CCU_AOT_LOG{D,I,W,E}`（前缀 `[ccu_aot]`）。host 侧使用 public runtime
 * 的同步接口，不再复制 adapter 的私有日志 helper。
 *
 * 注意：面向用户的**一行结论**（如 `VERIFY-*: PASS/FAIL/TIMEOUT`、`KERNEL-AUDIT:`）是结论行、
 * 不是日志，**不走本宏、不加级别**，保持内网一行反馈约定的可 grep 形态。
 */
#ifndef CCU_AOT_LOG_H
#define CCU_AOT_LOG_H

#include <cstdio>
#include <cstdlib>
#include <cstring>

enum CcuAotLogLevel {
    CCU_AOT_LL_DEBUG = 0,
    CCU_AOT_LL_INFO = 1,
    CCU_AOT_LL_WARN = 2,
    CCU_AOT_LL_ERROR = 3,
};

static inline const char* ccuAotLogLevelStr(int lvl)
{
    switch (lvl) {
        case CCU_AOT_LL_DEBUG:
            return "debug";
        case CCU_AOT_LL_WARN:
            return "warning";
        case CCU_AOT_LL_ERROR:
            return "error";
        default:
            return "info";
    }
}

/* 当前阈值（低于它的级别不输出）。读一次 CCU_AOT_LOG_LEVEL 并缓存；默认 info。 */
static inline int ccuAotLogThreshold(void)
{
    static int t = -1;
    if (t < 0) {
        const char* e = getenv("CCU_AOT_LOG_LEVEL");
        t = CCU_AOT_LL_WARN; /* 默认 warning（减噪） */
        if (e) {
            if (!strcmp(e, "debug"))
                t = CCU_AOT_LL_DEBUG;
            else if (!strcmp(e, "info"))
                t = CCU_AOT_LL_INFO;
            else if (!strcmp(e, "warning") || !strcmp(e, "warn"))
                t = CCU_AOT_LL_WARN;
            else if (!strcmp(e, "error"))
                t = CCU_AOT_LL_ERROR;
        }
    }
    return t;
}

/* 核心：带组件前缀。`fmt` 是日志自身信息（前缀 + 级别由宏补）。 */
#define CCU_AOT_LOG_AT(prefix, lvl, ...)                              \
    do {                                                              \
        if ((lvl) >= ccuAotLogThreshold()) {                          \
            fprintf(stderr, prefix " [%s] ", ccuAotLogLevelStr(lvl)); \
            fprintf(stderr, __VA_ARGS__);                             \
        }                                                             \
    } while (0)

/* adapter 专用（前缀 [ccu_aot]）。 */
#define CCU_AOT_LOGD(...) CCU_AOT_LOG_AT("[ccu_aot]", CCU_AOT_LL_DEBUG, __VA_ARGS__)
#define CCU_AOT_LOGI(...) CCU_AOT_LOG_AT("[ccu_aot]", CCU_AOT_LL_INFO, __VA_ARGS__)
#define CCU_AOT_LOGW(...) CCU_AOT_LOG_AT("[ccu_aot]", CCU_AOT_LL_WARN, __VA_ARGS__)
#define CCU_AOT_LOGE(...) CCU_AOT_LOG_AT("[ccu_aot]", CCU_AOT_LL_ERROR, __VA_ARGS__)

#endif /* CCU_AOT_LOG_H */
