# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
#

"""Compile the production CQE log with PRINTF's forwarding-reference signature."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class KernelLogCompileTest(unittest.TestCase):
    def test_cqe_bitfields_with_debug_printf(self):
        source = (
            ROOT / "src/aicore/hcomm/impl/platform_v310/hcomm_aiv_urma.h"
        ).read_text()
        marker = source.index('"Hcomm URMA CQE cqVa=')
        start = source.rfind("HCOMM_KERNEL_LOG(", 0, marker)
        end = source.index(";", marker) + 1
        statement = source[start:end]
        harness = (
            r"""
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCENDC_DEBUG
#include "src/aicore/hcomm/dfx/common/hcomm_dfx_impl.h"
#include "src/aicore/hcomm/common/hcomm_inner_def.h"
namespace AscendC {
template <typename... Args> void PRINTF(const char*, Args&&...) {}
}
#include "src/aicore/hcomm/common/hcomm_log.h"
int main() {
    AscendC::HcommUrmaJfcCqeCtx cqe{};
    auto* cqeUb = &cqe;
    uint64_t cqBaseAddr = 0;
    uint32_t curTail = 0;
"""
            + statement
            + "\n}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "log.cpp"
            path.write_text(harness)
            result = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsyntax-only",
                    "-I" + str(ROOT),
                    "-I" + str(ROOT / "include/aicore"),
                    "-I" + str(ROOT / "tests/host/stubs"),
                    str(path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
