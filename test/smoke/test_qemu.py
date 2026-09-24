import errno
import os
import pathlib
import pty
import select
import signal
import subprocess
import time

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PROMPT = b"> "
BOOT_TIMEOUT_SECONDS = 600
COMMAND_TIMEOUT_SECONDS = 60
SHUTDOWN_TIMEOUT_SECONDS = 60
DIAGNOSTIC_BYTES = 8 * 1024


class QemuSession:
    def __init__(self, target, trace_path):
        self.target = target
        self.trace_path = ROOT / trace_path
        self.transcript = bytearray()
        self.expect_offset = 0
        self.master_fd = None
        self.process = None

    def start(self):
        self.trace_path.unlink(missing_ok=True)
        master_fd, slave_fd = pty.openpty()
        try:
            self.process = subprocess.Popen(
                ["make", self.target],
                cwd=ROOT,
                stdin=slave_fd,
                stdout=slave_fd,
                stderr=slave_fd,
                start_new_session=True,
            )
        finally:
            os.close(slave_fd)
        self.master_fd = master_fd

    def expect(self, marker, timeout):
        start_offset = self.expect_offset
        deadline = time.monotonic() + timeout

        while True:
            marker_offset = self.transcript.find(marker, self.expect_offset)
            if marker_offset >= 0:
                self.expect_offset = marker_offset + len(marker)
                return bytes(self.transcript[start_offset:self.expect_offset])

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                pytest.fail(self._diagnostics(f"timed out waiting for {marker!r}"))

            if self.process.poll() is not None:
                self._read_available(0)
                pytest.fail(
                    self._diagnostics(
                        f"QEMU target exited with status {self.process.returncode} while waiting for {marker!r}"
                    )
                )

            self._read_available(min(remaining, 1.0))

    def send_line(self, line):
        os.write(self.master_fd, line.encode("ascii") + b"\n")

    def run_command(self, command):
        self.send_line(command)
        return self.expect(PROMPT, COMMAND_TIMEOUT_SECONDS)

    def wait_for_exit(self, timeout):
        deadline = time.monotonic() + timeout
        while self.process.poll() is None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                pytest.fail(self._diagnostics("timed out waiting for QEMU to shut down"))
            self._read_available(min(remaining, 1.0))

        self._read_available(0)
        if self.process.returncode != 0:
            pytest.fail(self._diagnostics(f"QEMU target exited with status {self.process.returncode}"))

    def close(self):
        if self.process is not None and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        if self.master_fd is not None:
            os.close(self.master_fd)
            self.master_fd = None

    def assert_trace(self):
        if not self.trace_path.is_file():
            pytest.fail(self._diagnostics(f"kernel trace was not created at {self.trace_path}"))

        trace = self.trace_path.read_bytes()
        if b"Kernel starting init:" not in trace:
            pytest.fail(self._diagnostics("kernel trace does not contain the init startup marker"))

    def _read_available(self, timeout):
        readable, _, _ = select.select([self.master_fd], [], [], timeout)
        if not readable:
            return
        try:
            chunk = os.read(self.master_fd, 64 * 1024)
        except OSError as error:
            if error.errno != errno.EIO:
                raise
            return
        self.transcript.extend(chunk)

    def _diagnostics(self, message):
        console_tail = bytes(self.transcript[-DIAGNOSTIC_BYTES:]).decode("utf-8", errors="replace")
        trace_tail = "<trace unavailable>"
        if self.trace_path.is_file():
            trace_tail = self.trace_path.read_bytes()[-DIAGNOSTIC_BYTES:].decode("utf-8", errors="replace")
        return f"{message}\n\nConsole tail:\n{console_tail}\n\nKernel trace tail:\n{trace_tail}"


@pytest.fixture
def qemu_session():
    sessions = []

    def create(target, trace_path):
        session = QemuSession(target, trace_path)
        sessions.append(session)
        session.start()
        return session

    yield create

    for session in sessions:
        session.close()


@pytest.mark.parametrize(
    ("target", "trace_path", "architecture_marker"),
    [
        pytest.param(
            "qemu-virt-hvf-trace",
            "build/aarch64/kernel-trace.log",
            b"interrupt_controller.redistributor_physical_base=",
            id="aarch64",
        ),
        pytest.param(
            "qemu-riscv64-trace",
            "build/riscv64/kernel-trace.log",
            b"interrupt_controller.plic.physical_base=",
            id="riscv64",
        ),
    ],
)
def test_qemu_login_commands_and_shutdown(qemu_session, target, trace_path, architecture_marker):
    session = qemu_session(target, trace_path)

    session.expect(b"login: ", BOOT_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(b"password: ", COMMAND_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(PROMPT, COMMAND_TIMEOUT_SECONDS)

    sysinfo = session.run_command("cat /boot/sysinfo")
    assert architecture_marker in sysinfo

    write_result = session.run_command("write /tmp/qemu-smoke smoke-ok")
    assert b"Command failed" not in write_result
    assert b"smoke-ok" in session.run_command("cat /tmp/qemu-smoke")

    processes = session.run_command("ps")
    assert b"/sbin/shell" in processes

    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)
    session.assert_trace()
