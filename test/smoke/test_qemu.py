import errno
import os
import pathlib
import pty
import re
import select
import signal
import shutil
import subprocess
import time

import pytest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PROMPT = b"bash-5.3# "
BOOT_TIMEOUT_SECONDS = 600
COMMAND_TIMEOUT_SECONDS = 60
SHUTDOWN_TIMEOUT_SECONDS = 60
DIAGNOSTIC_BYTES = 8 * 1024


class QemuSession:
    def __init__(self, target, trace_path, make_arguments=()):
        self.target = target
        self.make_arguments = make_arguments
        self.trace_path = ROOT / trace_path
        self.transcript = bytearray()
        self.expect_offset = 0
        self.master_fd = None
        self.process = None

    def start(self):
        self.trace_path.unlink(missing_ok=True)
        (self.trace_path.parent / "images" / "ext2.img").unlink(missing_ok=True)
        master_fd, slave_fd = pty.openpty()
        try:
            self.process = subprocess.Popen(
                ["make", self.target, *self.make_arguments],
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
        if b"Rust I/O router ready" not in trace:
            pytest.fail(self._diagnostics("kernel trace does not contain the I/O startup marker"))
        if b"process fault:" in trace:
            pytest.fail(self._diagnostics("kernel trace contains a process fault"))

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

    def create(target, trace_path, make_arguments=()):
        session = QemuSession(target, trace_path, make_arguments)
        sessions.append(session)
        session.start()
        return session

    yield create

    for session in sessions:
        session.close()


def test_trace_accepts_startup_without_verbose_kernel_logging(tmp_path):
    trace_path = tmp_path / "trace.log"
    trace_path.write_bytes(b"Rust I/O router ready")
    QemuSession("unused", str(trace_path)).assert_trace()


@pytest.mark.parametrize("trace", [b"", b"Kernel starting init:", b"Rust I/O router ready process fault:"])
def test_trace_rejects_missing_startup_and_process_faults(tmp_path, trace):
    trace_path = tmp_path / "trace.log"
    trace_path.write_bytes(trace)
    with pytest.raises(pytest.fail.Exception):
        QemuSession("unused", str(trace_path)).assert_trace()


def test_qemu_single_cpu_external_command_returns_prompt(qemu_session):
    session = qemu_session("qemu-virt-1cpu", "build/aarch64/qemu-virt-1cpu-unused.log")

    session.expect(b"login: ", BOOT_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(b"password: ", COMMAND_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(PROMPT, COMMAND_TIMEOUT_SECONDS)

    processes = session.run_command("ps")
    assert b"/usr/local/bin/ps" in processes

    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)


def assert_hosted_procfs(session):
    listing = session.run_command("/usr/local/bin/ls /proc")
    assert b"ktrace" in listing
    status = session.run_command("/usr/local/bin/cat /proc/1/status")
    assert b"pid=1" in status and b"sampled_at_usec=" in status
    for leaf, marker in (("cpu", b"total_usec="), ("memory", b"private_bytes="),
                         ("queue-wait", b"parks="), ("ipc", b"sampled_at_usec=")):
        contents = session.run_command(f"/usr/local/bin/cat /proc/1/{leaf}")
        assert marker in contents or b"unavailable" in contents
    inherited_proc = session.run_command("exec 3</proc/1/status; /usr/local/bin/cat <&3; exec 3<&-")
    assert b"pid=1" in inherited_proc
    trace_read = session.run_command("/usr/local/bin/cat /proc/ktrace > /tmp/proc-trace; printf 'proc-trace:%d\\n' $?")
    assert b"proc-trace:0" in trace_read


@pytest.mark.parametrize(
    ("target", "trace_path"),
    [
        ("qemu-virt-hvf-trace", "build/aarch64/kernel-trace.log"),
        ("qemu-riscv64-trace", "build/riscv64/kernel-trace.log"),
    ],
    ids=["aarch64", "riscv64"],
)
def test_qemu_rust_procfs_component(qemu_session, target, trace_path):
    session = qemu_session(target, trace_path)
    session.expect(b"login: ", BOOT_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(b"password: ", COMMAND_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(PROMPT, 2 * COMMAND_TIMEOUT_SECONDS)
    processes = session.run_command("ps")
    assert b"/sbin/services/hunde-io-host" in processes
    assert b"/sbin/services/hund-dateisystem" not in processes
    assert b"/sbin/services/hund-io-system" not in processes
    assert b"/sbin/drivers/procfs" not in processes
    assert b"/sbin/drivers/ext2fs" not in processes, processes
    assert b"/sbin/drivers/embedfs" not in processes, processes
    assert b"/sbin/drivers/sysinfofs" not in processes, processes
    migrated = session.run_command("/usr/local/bin/cat /mnt/channelio-ext2/hello.txt")
    assert b"hello from ext2" in migrated, migrated
    created = session.run_command("/usr/local/bin/mkdir /mnt/channelio-ext2/hosted-migration-proof")
    assert b"mkdir:" not in created, created
    removed = session.run_command("/usr/local/bin/rm -d /mnt/channelio-ext2/hosted-migration-proof")
    assert b"rm:" not in removed, removed
    hosted = session.run_command("/usr/local/bin/cat /mnt/hosted-ext2/hello.txt")
    assert b"HundeOS hosted EXT2 Wuff volume" in hosted
    executed = session.run_command(
        "/mnt/hosted-ext2/cat /mnt/hosted-ext2/hello.txt; printf 'hosted-exec:%d\\n' $?"
    )
    assert b"HundeOS hosted EXT2 Wuff volume" in executed, executed
    assert b"hosted-exec:0" in executed, executed
    created = session.run_command("/usr/local/bin/mkdir /mnt/hosted-ext2/wuff-proof")
    assert b"mkdir:" not in created, created
    listing = session.run_command("/usr/local/bin/ls /mnt/hosted-ext2")
    assert b"wuff-proof" in listing, listing
    removed = session.run_command("/usr/local/bin/rm -d /mnt/hosted-ext2/wuff-proof")
    assert b"rm:" not in removed, removed
    session.run_command("printf 'created-%s' hosted > /mnt/hosted-ext2/write-proof")
    session.run_command("printf '%s' '-appended' >> /mnt/hosted-ext2/write-proof")
    contents = session.run_command("/usr/local/bin/cat /mnt/hosted-ext2/write-proof")
    assert b"created-hosted-appended" in contents, contents
    session.run_command("printf '%s' short > /mnt/hosted-ext2/write-proof")
    contents = session.run_command("/usr/local/bin/cat /mnt/hosted-ext2/write-proof")
    assert b"short" in contents and b"appended" not in contents, contents
    copied = session.run_command("/usr/local/bin/cp /hello.txt /mnt/hosted-ext2/copy-proof")
    assert b"cp:" not in copied, copied
    assert b"hello from ext2" in session.run_command("/usr/local/bin/cat /mnt/hosted-ext2/copy-proof")
    removed = session.run_command("/usr/local/bin/rm /mnt/hosted-ext2/write-proof /mnt/hosted-ext2/copy-proof")
    assert b"rm:" not in removed, removed
    session.run_command("/usr/local/bin/mkdir -p /mnt/hosted-ext2/tree-proof/nested")
    session.run_command("printf '%s' linked-data > /mnt/hosted-ext2/tree-proof/nested/data")
    linked = session.run_command(
        "/usr/local/bin/ln /mnt/hosted-ext2/tree-proof/nested/data /mnt/hosted-ext2/link-proof; printf 'hosted-link:%d\\n' $?"
    )
    assert b"hosted-link:0" in linked, linked
    moved = session.run_command(
        "/usr/local/bin/mv /mnt/hosted-ext2/tree-proof/nested/data /mnt/hosted-ext2/tree-proof/nested/moved; printf 'hosted-move:%d\\n' $?"
    )
    assert b"hosted-move:0" in moved, moved
    removed = session.run_command("/usr/local/bin/rm -r /mnt/hosted-ext2/tree-proof; printf 'hosted-rmtree:%d\\n' $?")
    assert b"hosted-rmtree:0" in removed, removed
    assert b"linked-data" in session.run_command("/mnt/hosted-ext2/cat /mnt/hosted-ext2/link-proof")
    removed = session.run_command("/usr/local/bin/rm /mnt/hosted-ext2/link-proof")
    assert b"rm:" not in removed, removed
    exercise_file_follow(session, f"/mnt/hosted-ext2/tail-follow-{time.monotonic_ns()}")
    for volume in ("/mnt/channelio", "/mnt/channelio-ext2"):
        path = f"{volume}/TRUNCATE.TXT"
        session.run_command(f"printf '%s' long-original-value > {path}")
        session.run_command(f"printf '%s' q > {path}")
        contents = session.run_command(f"/usr/local/bin/cat {path}")
        assert b"q" in contents and b"long-original-value" not in contents, contents
        removed = session.run_command(f"/usr/local/bin/rm {path}")
        assert b"rm:" not in removed, removed
    assert_hosted_procfs(session)
    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)
    assert b"Rust procfs.wuff ready" in session.trace_path.read_bytes()
    assert b"Rust sysinfofs.wuff ready" in session.trace_path.read_bytes()


@pytest.mark.parametrize(
    ("target", "trace_path"),
    [
        ("qemu-virt-hvf-trace", "build/aarch64/kernel-trace.log"),
        ("qemu-riscv64-trace", "build/riscv64/kernel-trace.log"),
    ],
    ids=["aarch64", "riscv64"],
)
def test_qemu_nested_fork_and_filesystem_progress(qemu_session, target, trace_path):
    session = qemu_session(target, trace_path)
    session.expect(b"login: ", BOOT_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(b"password: ", COMMAND_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(PROMPT, 2 * COMMAND_TIMEOUT_SECONDS)

    session.run_command("file=/mnt/hosted-ext2/hello.txt")
    for batch in range(3):
        session.run_command(
            '(for index in 1 2 3 4 5; do /usr/local/bin/cat "$file" || exit 1; '
            'done) > /tmp/nested-reads & reader=$!'
        )
        pipeline = session.run_command("printf 'fork-pipe\\n' | /usr/local/bin/cat")
        assert b"fork-pipe" in pipeline and b"Cannot allocate memory" not in pipeline, pipeline
        completed = session.run_command('wait "$reader"; printf "nested-status:%s\\n" "$?"')
        assert b"nested-status:0" in completed, completed
        reads = session.run_command("/usr/local/bin/cat /tmp/nested-reads")
        assert reads.count(b"HundeOS hosted EXT2 Wuff volume") == 5, (batch, reads)

    for iteration in range(100):
        directory = f"irq-stress-{iteration}"
        path = f"/mnt/hosted-ext2/{directory}"
        created = session.run_command(f"/usr/local/bin/mkdir {path}")
        assert b"mkdir:" not in created, (iteration, created)
        listing = session.run_command("/usr/local/bin/ls /mnt/hosted-ext2")
        assert directory.encode() in listing, (iteration, listing)
        removed = session.run_command(f"/usr/local/bin/rm -d {path}")
        assert b"rm:" not in removed, (iteration, removed)

    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)


@pytest.mark.parametrize(
    ("target", "trace_path"),
    [
        ("qemu-virt-hvf-trace", "build/aarch64/kernel-trace.log"),
        ("qemu-riscv64-trace", "build/riscv64/kernel-trace.log"),
    ],
    ids=["aarch64", "riscv64"],
)
def test_qemu_rust_socket_component(qemu_session, target, trace_path):
    session = qemu_session(target, trace_path)
    session.expect(b"login: ", BOOT_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(b"password: ", COMMAND_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(PROMPT, 2 * COMMAND_TIMEOUT_SECONDS)

    processes = session.run_command("ps")
    assert b"/sbin/services/hunde-io-host" in processes
    assert b"/sbin/drivers/socket_router" not in processes
    drivers = session.run_command("/usr/local/bin/ls /sbin/drivers")
    assert b"uart_rx" in drivers and b"uart_tx" in drivers, drivers
    assert b"socket_router" not in drivers, drivers
    assert b"/sbin/services/hund-rohr" not in processes
    assert b"/sbin/drivers/memfs" not in processes
    assert b"/sbin/drivers/procfs" not in processes
    assert_hosted_procfs(session)

    hosted_reads = "/mnt/hosted-ext2/hello.txt{" + "," * 19 + "}"
    session.run_command(f"/usr/local/bin/cat {hosted_reads} > /tmp/hosted-reads & io_reader=$!")
    pipeline = session.run_command("printf 'pipe-%s\\n' hosted | /usr/local/bin/cat | /usr/local/bin/cat")
    assert b"pipe-hosted" in pipeline
    builtin = session.run_command("printf 'fork-%s\\n' inherited | { read value; printf '%s\\n' \"$value\"; }")
    assert b"fork-inherited" in builtin
    bulk = session.run_command(
        "for ((index=0;index<70;index++)); do printf '%01000d' 0; done | "
        "{ read -r -N 70000 value; printf 'pipe-count:%s\\n' \"${#value}\"; }"
    )
    assert b"pipe-count:70000" in bulk

    redirected = session.run_command(
        "printf 'memfs-%s\\n' hosted | /usr/local/bin/cat > /tmp/hosted-file"
    )
    assert b"Function not implemented" not in redirected
    assert b"memfs-hosted" in session.run_command("/usr/local/bin/cat /tmp/hosted-file")
    session.run_command("printf 'append-%s\\n' ok >> /tmp/hosted-file")
    contents = session.run_command("/usr/local/bin/cat /tmp/hosted-file")
    assert b"memfs-hosted" in contents and b"append-ok" in contents
    session.run_command("/usr/local/bin/mkdir -p /tmp/hosted-dir/nested")
    session.run_command(
        "for index in 1 2 3 4 5; do printf '%s' data > /tmp/hosted-dir/nested/file$index; done"
    )
    listing = session.run_command("/usr/local/bin/ls /tmp/hosted-dir/nested")
    for index in range(1, 6):
        assert f"file{index}".encode() in listing
    session.run_command("/usr/local/bin/cp /hello.txt /tmp/hosted-copy")
    assert b"hello from ext2" in session.run_command("/usr/local/bin/cat /tmp/hosted-copy")
    session.run_command("/usr/local/bin/rm -r /tmp/hosted-dir")

    for suffix in ("first", "second"):
        uri = f"socket://rust-smoke/{suffix}"
        session.send_line(f"socket_test listen {uri} &")
        session.expect(f"socket_test: listening {uri}".encode(), COMMAND_TIMEOUT_SECONDS)
        session.send_line("printf 'coexist-%s\\n' pipes | /usr/local/bin/cat")
        session.expect(b"coexist-pipes", COMMAND_TIMEOUT_SECONDS)
        session.expect(PROMPT, COMMAND_TIMEOUT_SECONDS)
        session.send_line(f"socket_test connect {uri} rust-ping")
        exchange = session.expect(b"socket_test: reply pong", COMMAND_TIMEOUT_SECONDS)
        assert b"socket_test: received rust-ping" in exchange
        session.expect(PROMPT, COMMAND_TIMEOUT_SECONDS)

    completed = session.run_command('wait "$io_reader"; printf "hosted-read-status:%s\\n" "$?"')
    assert b"hosted-read-status:0" in completed, completed
    reads = session.run_command("/usr/local/bin/cat /tmp/hosted-reads")
    assert reads.count(b"HundeOS hosted EXT2 Wuff volume") == 20, reads

    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)
    trace = session.trace_path.read_bytes()
    assert b"Rust socket.wuff ready" in trace
    assert b"Rust pipe.wuff ready" in trace
    assert b"Rust memfs.wuff ready" in trace
    assert b"Rust procfs.wuff ready" in trace


def exercise_file_follow(session, follow_path):
    follow_output = f"/tmp/tail-output-{time.monotonic_ns()}"
    session.run_command(f"write {follow_path} seed")
    session.run_command(f"/usr/local/bin/tail -f {follow_path} > {follow_output} & tail_pid=$!")
    for expected in (b"seed", b"seedfollowed"):
        deadline = time.monotonic() + COMMAND_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            followed = session.run_command(f"/usr/local/bin/cat {follow_output}")
            if expected in followed:
                break
            time.sleep(0.1)
        else:
            pytest.fail(session._diagnostics(f"tail did not produce {expected!r}"))
        if expected == b"seed":
            session.run_command(f"printf '%s' seedfollowed > {follow_path}")
    session.run_command('kill "$tail_pid"; wait "$tail_pid"')
    session.run_command(f"/usr/local/bin/rm {follow_path} {follow_output}")


ROOT_FILESYSTEM_TARGETS = [
    pytest.param(
        "qemu-virt-hvf-trace",
        "build/aarch64/kernel-trace.log",
        b"interrupt_controller.redistributor_physical_base=",
        COMMAND_TIMEOUT_SECONDS,
        id="aarch64",
    ),
    pytest.param(
        "qemu-riscv64-trace",
        "build/riscv64/kernel-trace.log",
        b"interrupt_controller.plic.physical_base=",
        2 * COMMAND_TIMEOUT_SECONDS,
        id="riscv64",
    ),
]


@pytest.mark.parametrize(
    ("target", "trace_path", "architecture_marker", "shell_timeout"), ROOT_FILESYSTEM_TARGETS
)
def test_qemu_hosted_fat32_wuff(qemu_session, tmp_path, target, trace_path, architecture_marker, shell_timeout):
    configuration = "qemu_riscv64" if target == "qemu-riscv64-trace" else "qemu_virt_hvf"
    directory = tmp_path / "boot-config"
    shutil.copytree(ROOT / "config" / configuration, directory)
    with (directory / "fstab").open("a") as fstab:
        fstab.write("\ndevice /dev/virtio3 fat32.wuff /mnt/fat\n")
    boot_image = tmp_path / "boot-config.img"
    subprocess.run(
        ["genext2fs", "-b", "8192", "-B", "1024", "-L", "BOOTCFG", "-d", str(directory), str(boot_image)],
        check=True,
    )
    session = qemu_session(target, trace_path, (
        f"BOOT_CONFIG_EXT2_IMG={boot_image}",
        f"HVF_BOOT_CONFIG_EXT2_IMG={boot_image}",
        f"DISK_IMG={tmp_path / 'fat32.img'}",
    ))
    session.expect(b"login: ", BOOT_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(b"password: ", COMMAND_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(PROMPT, shell_timeout)
    listing = session.run_command("/usr/local/bin/ls /mnt/fat")
    assert b"TEST.TXT" in listing, listing
    assert b"hello from a long FAT32 filename" in session.run_command("/usr/local/bin/cat '/mnt/fat/This is a long filename.txt'")
    assert b"/sbin/drivers/fat32fs" not in session.run_command("ps")
    assert b"fat32fs" not in session.run_command("/usr/local/bin/ls /sbin/drivers")
    assert b"Nested file on the FAT32 SD card test image" in session.run_command("/usr/local/bin/cat /mnt/sd/DOCS/NOTE.TXT")
    assert b"hello" in session.run_command("/usr/local/bin/cat /mnt/channelio/CHANNEL.TXT")
    assert b"fat32.wuff" in session.run_command("/usr/local/bin/ls /sbin/components")
    assert b"hello from ext2" in session.run_command("/usr/local/bin/cat /hello.txt")
    assert b"fat32-mkdir:0" in session.run_command("/usr/local/bin/mkdir /mnt/fat/PROBE; printf 'fat32-mkdir:%d\\n' $?")
    result = session.run_command("printf '%s' first > /mnt/fat/PROBE/DATA.TXT; printf 'fat32-write:%d\\n' $?")
    assert b"fat32-write:0" in result, result
    session.run_command("printf '%s' '-second' >> /mnt/fat/PROBE/DATA.TXT")
    assert b"first-second" in session.run_command("/usr/local/bin/cat /mnt/fat/PROBE/DATA.TXT")
    inherited = session.run_command("exec 3</mnt/fat/PROBE/DATA.TXT; /usr/local/bin/cat <&3; exec 3<&-")
    assert b"first-second" in inherited, inherited
    session.run_command("printf '%s' short > /mnt/fat/PROBE/DATA.TXT")
    result = session.run_command("/usr/local/bin/cat /mnt/fat/PROBE/DATA.TXT")
    assert b"short" in result and b"first-second" not in result, result
    assert b"fat32-copy:0" in session.run_command("/usr/local/bin/cp /hello.txt /mnt/fat/COPY.TXT; printf 'fat32-copy:%d\\n' $?")
    assert b"hello from ext2" in session.run_command("/usr/local/bin/cat /mnt/fat/COPY.TXT")
    assert b"fat32-remove:0" in session.run_command("/usr/local/bin/rm /mnt/fat/PROBE/DATA.TXT /mnt/fat/COPY.TXT; printf 'fat32-remove:%d\\n' $?")
    assert b"fat32-rmdir:0" in session.run_command("/usr/local/bin/rm -d /mnt/fat/PROBE; printf 'fat32-rmdir:%d\\n' $?")
    assert b"fat32-exec-copy:0" in session.run_command("/usr/local/bin/cp /usr/local/bin/cat /mnt/fat/CAT; printf 'fat32-exec-copy:%d\\n' $?")
    assert b"hello from a long FAT32 filename" in session.run_command("/mnt/fat/CAT '/mnt/fat/This is a long filename.txt'")
    session.run_command("/usr/local/bin/mkdir /mnt/fat/TREE /mnt/fat/TREE/CHILD")
    session.run_command("printf '%s' recursive > /mnt/fat/TREE/CHILD/DATA.TXT")
    assert b"fat32-recursive:0" in session.run_command("/usr/local/bin/rm -r /mnt/fat/TREE; printf 'fat32-recursive:%d\\n' $?")
    session.run_command("/usr/local/bin/rm /mnt/fat/CAT")
    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)
    session.assert_trace()
    trace = session.trace_path.read_bytes()
    assert b"Rust fat32.wuff ready" in trace
    assert b"unsupported filesystem fat32.wuff" not in trace


def exercise_root_filesystem_commands(session, architecture_marker, shell_timeout):
    session.expect(b"login: ", BOOT_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(b"password: ", COMMAND_TIMEOUT_SECONDS)
    session.send_line("root")
    session.expect(PROMPT, shell_timeout)
    workspace = f"/bsd-smoke-{time.monotonic_ns()}"

    sysinfo = session.run_command("/usr/local/bin/cat /boot/sysinfo")
    assert architecture_marker in sysinfo
    assert b"cpu.count=4" in sysinfo, sysinfo

    processes = session.run_command("ps")
    assert b"/sbin/services/hund-unterbrict" in processes
    assert processes.count(b"/sbin/services/hunde-io-host") == 1
    assert b"/sbin/services/hund-dateisystem" not in processes
    assert b"/sbin/services/hund-io-system" not in processes
    assert b"/sbin/services/hund-verfolgen" not in processes
    assert b"/sbin/drivers/virtio_blk_channelio" not in processes
    assert b"/sbin/drivers/ext2fs" not in processes, processes
    assert b"/sbin/drivers/fat32fs" not in processes, processes

    bootstrap = session.run_command("/usr/local/bin/ls /sbin/components")
    assert b"ext2fs.wuff" in bootstrap
    mounts = session.run_command("/usr/local/bin/cat /boot/config/fstab")
    assert b"device /dev/virtio1 ext2.wuff /" in mounts
    assert b"ext2.wuff /mnt/channelio-ext2" in mounts

    numbered = session.run_command("/usr/local/bin/cat -n /hello.txt")
    assert b"1\thello from ext2" in numbered

    default_listing = session.run_command("/usr/local/bin/ls")
    assert b"hello.txt" in default_listing
    assert b"usr" in default_listing

    root_listing = session.run_command("/usr/local/bin/ls /")
    assert b"hello.txt" in root_listing
    assert b"usr" in root_listing

    commands = session.run_command("/usr/local/bin/ls -1 /usr/local/bin")
    for command in (b"cat", b"cp", b"grep", b"ln", b"ls", b"mkdir", b"mv", b"rm", b"tail"):
        assert command in commands

    setjmp_probe = session.run_command("setjmp_probe")
    assert b"setjmp_probe: ok" in setjmp_probe

    libc_probe = session.run_command("libc_probe")
    assert b"libc_probe: ok" in libc_probe

    grepped = session.run_command("/usr/local/bin/grep -n hello.*ext2 /hello.txt")
    assert b"1:hello from ext2" in grepped

    tailed = session.run_command("/usr/local/bin/tail -n 1 /hello.txt")
    assert b"hello from ext2" in tailed

    exercise_file_follow(session, f"/tail-follow-{time.monotonic_ns()}")

    mkdir_result = session.run_command(f"/usr/local/bin/mkdir -p {workspace}/a/b")
    assert b"mkdir:" not in mkdir_result, mkdir_result
    write_result = session.run_command(f"write {workspace}/a/b/source bsd-tools")
    assert b"write:" not in write_result, write_result
    copy_result = session.run_command(f"/usr/local/bin/cp {workspace}/a/b/source {workspace}/a/b/copy")
    assert b"cp:" not in copy_result, copy_result
    assert b"bsd-tools" in session.run_command(f"/usr/local/bin/cat {workspace}/a/b/copy")
    binary_copy_result = session.run_command(f"/usr/local/bin/cp /usr/local/bin/cat {workspace}/cat-copy")
    assert b"cp:" not in binary_copy_result, binary_copy_result
    source_listing = session.run_command("/usr/local/bin/ls -l /usr/local/bin/cat")
    copied_listing = session.run_command(f"/usr/local/bin/ls -l {workspace}/cat-copy")
    source_line = next(
        line for line in source_listing.splitlines() if line.strip().startswith(b"-")
    )
    copied_path = f"{workspace}/cat-copy".encode()
    copied_line = next(
        line for line in copied_listing.splitlines() if line.strip().startswith(b"-")
    )
    source_size = int(source_line.split()[4])
    copied_size = int(copied_line.split()[4])
    assert copied_size == source_size
    assert copied_size > 12 * 1024
    copied_marker = session.run_command(f"/usr/local/bin/grep -al raw_cat {workspace}/cat-copy")
    assert copied_path in copied_marker
    overwrite_result = session.run_command(f"/usr/local/bin/cp {workspace}/a/b/source {workspace}/a/b/copy")
    assert b"cp:" not in overwrite_result, overwrite_result
    session.run_command(f"/usr/local/bin/mkdir {workspace}/copies")
    multiple_result = session.run_command(
        f"/usr/local/bin/cp {workspace}/a/b/source {workspace}/a/b/copy {workspace}/copies"
    )
    assert b"cp:" not in multiple_result, multiple_result
    recursive_result = session.run_command(f"/usr/local/bin/cp -R {workspace}/a {workspace}/recursive")
    assert b"cp:" not in recursive_result, recursive_result
    assert b"bsd-tools" in session.run_command(f"/usr/local/bin/cat {workspace}/recursive/b/source")
    link_result = session.run_command(f"/usr/local/bin/ln {workspace}/a/b/source {workspace}/a/b/link")
    assert b"ln:" not in link_result, link_result
    assert b"bsd-tools" in session.run_command(f"/usr/local/bin/cat {workspace}/a/b/link")
    move_result = session.run_command(f"/usr/local/bin/mv {workspace}/a {workspace}/moved")
    assert b"mv:" not in move_result, move_result.decode("utf-8", errors="replace")
    assert b"bsd-tools" in session.run_command(f"/usr/local/bin/cat {workspace}/moved/b/source")
    moved_listing = session.run_command(f"/usr/local/bin/ls -1 {workspace}/moved/b")
    assert b"link" in moved_listing
    assert b"source" in moved_listing

    secure_remove = session.run_command(f"/usr/local/bin/rm -P {workspace}/moved/b/source")
    assert b"-P is not supported" in secure_remove
    assert b"bsd-tools" in session.run_command(f"/usr/local/bin/cat {workspace}/moved/b/source")

    session.run_command(f"printf '%s' original-contents > {workspace}/truncate-source")
    session.run_command(f"/usr/local/bin/ln {workspace}/truncate-source {workspace}/truncate-alias")
    session.run_command(f"exec 3<{workspace}/truncate-source")
    session.run_command(f"printf '%s' shortened > {workspace}/truncate-source")
    alias = session.run_command(f"/usr/local/bin/cat {workspace}/truncate-alias")
    assert b"shortened" in alias and b"original-contents" not in alias, alias
    inherited = session.run_command("/usr/local/bin/cat <&3; exec 3<&-")
    assert b"shortened" in inherited and b"original-contents" not in inherited, inherited

    remove_link = session.run_command(f"/usr/local/bin/rm -v {workspace}/moved/b/link")
    assert f"{workspace}/moved/b/link".encode() in remove_link
    force_missing = session.run_command(f"/usr/local/bin/rm -f {workspace}/missing")
    assert b"rm:" not in force_missing, force_missing

    recursive_remove = session.run_command(f"/usr/local/bin/rm -rv {workspace}")
    assert f"{workspace}/moved/b/source".encode() in recursive_remove
    assert workspace.encode() in recursive_remove
    missing_workspace = session.run_command(f"/usr/local/bin/ls -1 {workspace}")
    assert b"ls:" in missing_workspace, missing_workspace

    write_result = session.run_command("write /tmp/qemu-smoke smoke-ok")
    assert b"Command failed" not in write_result
    assert b"smoke-ok" in session.run_command("cat /tmp/qemu-smoke")

@pytest.mark.parametrize(
    ("target", "trace_path", "architecture_marker", "shell_timeout"), ROOT_FILESYSTEM_TARGETS
)
def test_qemu_root_filesystem_and_shutdown(qemu_session, target, trace_path, architecture_marker, shell_timeout):
    session = qemu_session(target, trace_path)
    exercise_root_filesystem_commands(session, architecture_marker, shell_timeout)
    config = session.run_command("cat /boot/config/fstab")
    assert b"device uart_rx,uart_tx console /dev/tty0" in config
    assert b"device /dev/tty0 console :console:" in config
    listing = session.run_command("ls /dev")
    assert b"tty0" in listing
    assert b"null" in listing and b"virtio1" in listing and b"hd0" in listing, listing
    processes = session.run_command("ps")
    assert b"/sbin/drivers/devfs" not in processes
    metadata = session.run_command("cat /dev/virtio1")
    assert b"path=/dev/virtio1" in metadata and b"block_size=512" in metadata
    alias_metadata = session.run_command("cat /dev/hd0")
    assert b"path=/dev/virtio1" in alias_metadata and b"block_size=512" in alias_metadata
    inherited = session.run_command("exec 3</dev/virtio1; /usr/local/bin/cat <&3; exec 3<&-")
    assert b"path=/dev/virtio1" in inherited
    null_io = session.run_command("exec 3<>/dev/null; printf '\\144\\151\\163\\143\\141\\162\\144\\145\\144\\012' >&3; /usr/local/bin/cat <&3; exec 3<&-; exec 3>&-; printf 'null-io-%s\\n' ready")
    assert b"null-io-ready" in null_io and b"\r\ndiscarded\r\n" not in null_io
    output = session.run_command("printf 'console-node-%s\\n' ready > /dev/tty0")
    assert b"console-node-ready" in output
    output = session.run_command("exec 3<>/dev/tty0; printf 'console-duplex-%s\\n' ready >&3; exec 3<&-; exec 3>&-")
    assert b"console-duplex-ready" in output
    session.send_line("read -r -n 1 console_key </dev/tty0; printf 'console-read-%s\\n' \"$console_key\"")
    session.send_line("Q")
    session.expect(b"console-read-Q", shell_timeout)
    session.expect(PROMPT, shell_timeout)
    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)
    trace = session.trace_path.read_bytes()
    assert b"Rust ext2fs.wuff ready" in trace
    assert b"Rust filesystem namespace ready" in trace
    assert b"Rust I/O router ready" in trace
    assert b"Rust hosted devfs ready" in trace
    session.assert_trace()


@pytest.mark.parametrize(
    ("target", "trace_path", "architecture_marker", "shell_timeout"), ROOT_FILESYSTEM_TARGETS
)
def test_qemu_login_commands_and_shutdown(qemu_session, target, trace_path, architecture_marker, shell_timeout):
    session = qemu_session(target, trace_path)
    exercise_root_filesystem_commands(session, architecture_marker, shell_timeout)
    session.send_line("/usr/local/bin/cat")
    session.send_line("interrupt-me")
    session.expect(b"interrupt-me", COMMAND_TIMEOUT_SECONDS)
    os.write(session.master_fd, b"\x03")
    session.expect(PROMPT, COMMAND_TIMEOUT_SECONDS)

    os.write(session.master_fd, b"partial\x03")
    cancelled = session.expect(PROMPT, COMMAND_TIMEOUT_SECONDS)
    assert b"partial^C" in cancelled

    processes = session.run_command("ps")
    assert b"/usr/local/bin/bash" in processes

    session.send_line("shutdown")
    session.wait_for_exit(SHUTDOWN_TIMEOUT_SECONDS)
    session.assert_trace()
