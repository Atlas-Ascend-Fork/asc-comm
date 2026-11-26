#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Preserve process output and optionally mirror it to the live console.

Log files keep the original bytes. Console decoding and rank prefixes affect
only the mirror. The caller retains ownership of timeout and process-group
cleanup; its cleanup callback runs before the final bounded pipe drain.
"""

import codecs
from contextlib import contextmanager
import os
from pathlib import Path
import selectors
import subprocess
import sys
import threading
import time


_OUTPUT_LOCK = threading.Lock()


class _ConsoleMirror:
    def __init__(self, stream, prefix):
        self.stream = stream
        self.prefix = prefix
        self.at_line_start = True
        self.decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")

    def write(self, data, *, final=False):
        text = self.decoder.decode(data, final=final)
        if not text or self.stream is None:
            return
        parts = []
        for line in text.splitlines(keepends=True):
            if self.at_line_start:
                parts.append(self.prefix)
            parts.append(line)
            self.at_line_start = line.endswith("\n")
        try:
            with _OUTPUT_LOCK:
                self.stream.write("".join(parts))
                self.stream.flush()
        except (OSError, ValueError):
            # Closing a terminal or consumer must not stop draining the child
            # or discard the on-disk evidence.
            self.stream = None


class _Capture:
    def __init__(
        self,
        command,
        stdout_path,
        stderr_path,
        *,
        cwd,
        env,
        start_new_session,
        pass_fds,
        echo_stdout,
        echo_stderr,
        prefix,
        cleanup,
    ):
        self.stop = threading.Event()
        self.errors = []
        self.selector = selectors.DefaultSelector()
        self.logs = []
        self.process = None
        self.thread = None
        try:
            self.logs.append(Path(stdout_path).open("xb"))
            self.logs.append(Path(stderr_path).open("xb"))
            self.process = subprocess.Popen(
                command,
                cwd=str(cwd),
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                start_new_session=start_new_session,
                pass_fds=pass_fds,
            )
            for pipe, log, target in (
                (
                    self.process.stdout,
                    self.logs[0],
                    sys.stdout if echo_stdout else None,
                ),
                (
                    self.process.stderr,
                    self.logs[1],
                    sys.stderr if echo_stderr else None,
                ),
            ):
                os.set_blocking(pipe.fileno(), False)
                self.selector.register(
                    pipe, selectors.EVENT_READ, (log, _ConsoleMirror(target, prefix))
                )
            self.thread = threading.Thread(target=self._pump, daemon=True)
            self.thread.start()
        except BaseException:
            try:
                if self.process is not None:
                    if cleanup is not None:
                        cleanup(self.process)
                    elif self.process.poll() is None:
                        self.process.kill()
                        self.process.wait(timeout=3)
            finally:
                self.selector.close()
                for log in self.logs:
                    log.close()
                if self.process is not None:
                    for pipe in (self.process.stdout, self.process.stderr):
                        pipe.close()
            raise

    def _read(self, key):
        try:
            data = os.read(key.fileobj.fileno(), 65536)
        except BlockingIOError:
            return
        if not data:
            self.selector.unregister(key.fileobj)
            key.data[1].write(b"", final=True)
            key.fileobj.close()
            return
        log, mirror = key.data
        try:
            log.write(data)
            log.flush()
        except OSError as error:
            if not self.errors:
                self.errors.append(error)
            # Keep draining to avoid blocking the process on a full pipe.
        mirror.write(data)

    def _pump(self):
        try:
            while self.selector.get_map() and not self.stop.is_set():
                for key, _events in self.selector.select(timeout=0.05):
                    self._read(key)
            # A detached descendant can keep a pipe open after the process
            # group has been cleaned up. Drain what is already buffered, then
            # stop without waiting indefinitely for that descendant's EOF.
            deadline = time.monotonic() + 0.1
            while self.selector.get_map() and time.monotonic() < deadline:
                ready = self.selector.select(timeout=0)
                if not ready:
                    break
                for key, _events in ready:
                    self._read(key)
        except BaseException as error:
            if not self.errors:
                self.errors.append(error)
        finally:
            for key in list(self.selector.get_map().values()):
                key.data[1].write(b"", final=True)
                key.fileobj.close()
            self.selector.close()
            for log in self.logs:
                try:
                    log.close()
                except OSError as error:
                    if not self.errors:
                        self.errors.append(error)

    def finish(self):
        self.thread.join(timeout=1)
        if self.thread.is_alive():
            self.stop.set()
            self.thread.join(timeout=0.3)
        if self.thread.is_alive():
            raise RuntimeError("output capture did not stop within its drain deadline")
        if self.errors:
            raise RuntimeError(
                "failed to preserve process output: " + str(self.errors[0])
            ) from self.errors[0]


@contextmanager
def capture_process_output(
    command,
    stdout_path,
    stderr_path,
    *,
    cwd,
    env=None,
    start_new_session=True,
    pass_fds=(),
    echo_stdout=False,
    echo_stderr=False,
    prefix="",
    cleanup=None,
):
    """Yield Popen while preserving and optionally displaying its output.

    The caller waits with its existing timeout and supplies process-group cleanup
    when needed. Cleanup runs before draining, even if the body raises. Console
    mirroring never changes archived bytes or the process return code.
    """
    capture = _Capture(
        command,
        stdout_path,
        stderr_path,
        cwd=cwd,
        env=env,
        start_new_session=start_new_session,
        pass_fds=pass_fds,
        echo_stdout=echo_stdout,
        echo_stderr=echo_stderr,
        prefix=prefix,
        cleanup=cleanup,
    )
    try:
        yield capture.process
    finally:
        try:
            if cleanup is not None:
                cleanup(capture.process)
        finally:
            capture.finish()
