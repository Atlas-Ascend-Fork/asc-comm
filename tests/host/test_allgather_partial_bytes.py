# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest


ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ("02_allgather_mesh1d_mem2mem", "03_allgather_nhr1d_mem2mem")


@pytest.mark.parametrize("example", EXAMPLES)
@pytest.mark.parametrize("corrupt_file", [None, "golden.bin", "output_rank_0.bin"])
@pytest.mark.parametrize("tail_size", [1, 3])
def test_allgather_rejects_partial_fp32_bytes(
    tmp_path, example, corrupt_file, tail_size
):
    for name in ("golden.bin", "output_rank_0.bin"):
        path = tmp_path / name
        np.array([1.0], dtype=np.float32).tofile(path)
        if name == corrupt_file:
            path.write_bytes(path.read_bytes() + bytes(tail_size))
    script = ROOT / "examples/ccu/ccu_custom" / example / "scripts/verify_result.py"
    result = subprocess.run(
        [
            sys.executable,
            str(script),
            "--rank-size",
            "1",
            "--count",
            "1",
            "--output-dir",
            str(tmp_path),
            "--golden",
            str(tmp_path / "golden.bin"),
        ],
        capture_output=True,
        text=True,
        timeout=10,
    )
    if corrupt_file is None:
        assert result.returncode == 0, result.stdout + result.stderr
        assert "test pass!" in result.stdout
    else:
        assert result.returncode != 0, result.stdout + result.stderr
        assert "byte size mismatch" in result.stdout
