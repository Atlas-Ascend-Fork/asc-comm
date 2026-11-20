#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Use the shared guarded deployment protocol for the isolated CCU package."""

import package_deploy


def make_config(**kwargs):
    return package_deploy.make_config(package_profile="ccu", **kwargs)


class Deployment(package_deploy.Deployment):
    def __init__(self, config, *, verify_existing=False):
        if package_deploy.package_profile(config) != "ccu":
            raise ValueError("CCU deployment requires a CCU package configuration")
        super().__init__(config, verify_existing=verify_existing)
