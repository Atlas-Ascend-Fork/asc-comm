#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# Copied from hccl_api_test PR43, 3c699a80e8d8e2801f9c4d5baed99d92af2b5509.
# Original: scripts/ccu_probe_markers.py.

"""Seven KFC probe signatures emitted by the instrumented MC2 server."""

PROBE_GAPS = (
    (4, 3, 1, 1, 2, 3, 3),  # Algorithm start.
    (4, 3, 1, 1, 3, 3, 2),  # Algorithm end.
    (4, 3, 1, 2, 1, 3, 3),  # Queue head start.
    (4, 3, 1, 2, 3, 3, 1),  # Queue head end.
    (4, 3, 1, 3, 3, 1, 2),  # Parameter load start.
    (4, 3, 1, 3, 3, 2, 1),  # Turn-end signal complete.
    (4, 3, 2, 1, 1, 3, 3),  # Queue end.
)
PROBE_KINDS = {gaps: kind for kind, gaps in enumerate(PROBE_GAPS)}


def find_probes(waits):
    """Return (kind, first Record start, last Wait end) on one mission lane."""
    waits = sorted(waits, key=lambda wait: (wait[1], wait[0]))
    marks = []
    for index in range(len(waits) - 7):
        block = waits[index : index + 8]
        deltas = tuple(b[0] - a[0] for a, b in zip(block, block[1:]))
        kind = PROBE_KINDS.get(deltas)
        if kind is not None:
            marks.append((kind, block[0][1], block[-1][2]))
    return marks


SOURCE_COMMIT = "3c699a80e8d8e2801f9c4d5baed99d92af2b5509"
