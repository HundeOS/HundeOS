import json
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

import compile_commands


ROOT = Path(__file__).resolve().parents[2]


class FakeRunner:
    def __init__(self, output, returncode=0, error=""):
        self.output = output
        self.returncode = returncode
        self.error = error
        self.calls = []
        self.environments = []

    def __call__(self, command, *, cwd, text, capture_output, check, env):
        self.calls.append((command, cwd, text, capture_output, check))
        self.environments.append(env)
        return subprocess.CompletedProcess(command, self.returncode, self.output, self.error)


@pytest.fixture
def source(tmp_path):
    source = tmp_path / "source.c"
    source.write_text("int value;\n", encoding="utf-8")
    return source


def test_capture_keeps_architecture_pic_and_output_variants(source):
    commands = [
        "cc -c -DARCH_AARCH64 -fno-pic source.c -o static.o",
        "cc -c -DARCH_AARCH64 -fPIC source.c -o pic.o",
        "cc -c -DARCH_RISCV64 -fPIC source.c -o riscv.o",
        "cc -c -DARCH_AARCH64 -fPIC source.c -o pic.o",
    ]
    runner = FakeRunner("\n".join(commands) + "\n")
    entries = compile_commands.capture(source.parent, ["make", "capture"], runner)
    assert len(entries) == 3
    assert [entry["arguments"][-1] for entry in entries] == ["static.o", "pic.o", "riscv.o"]
    assert all(Path(entry["arguments"][0]).is_absolute() for entry in entries)
    assert runner.calls == [(["make", "capture"], source.parent, True, True, False)]


def test_failed_make_preserves_the_existing_database_even_with_partial_commands(source):
    database = source.parent / "compile_commands.json"
    original = '[{"previous": true}]\n'
    database.write_text(original, encoding="utf-8")
    runner = FakeRunner("cc -c source.c -o partial.o\n", returncode=2, error="Newlib capture failed\n")
    with pytest.raises(compile_commands.CaptureError, match="Newlib capture failed"):
        compile_commands.generate(source.parent, database, ["make"], runner)
    assert database.read_text(encoding="utf-8") == original
    assert not list(source.parent.glob(".compile_commands.json.*"))


def test_command_line_returns_failure_and_keeps_the_old_database(tmp_path):
    tools = tmp_path / "tools"
    tools.mkdir()
    script = tools / "compile_commands.py"
    shutil.copyfile(compile_commands.__file__, script)
    database = tmp_path / "compile_commands.json"
    database.write_text("previous database\n", encoding="utf-8")
    fake_make = tmp_path / "failing-make"
    fake_make.write_text("#!/bin/sh\nprintf 'cc -c source.c -o source.o\\n'\nprintf 'make failure\\n' >&2\nexit 2\n", encoding="utf-8")
    fake_make.chmod(0o755)
    result = subprocess.run([sys.executable, str(script), "--make", str(fake_make)],
                            capture_output=True, text=True, check=False)
    assert result.returncode == 1
    assert "make failure" in result.stderr
    assert database.read_text(encoding="utf-8") == "previous database\n"


def test_empty_capture_is_an_error_and_does_not_publish_a_database(tmp_path):
    database = tmp_path / "compile_commands.json"
    with pytest.raises(compile_commands.CaptureError, match="no compiler commands"):
        compile_commands.generate(tmp_path, database, ["make"], FakeRunner("nothing to compile\n"))
    assert not database.exists()


def test_success_replaces_old_entries_without_leaving_temporary_files(source):
    database = source.parent / "compile_commands.json"
    database.write_text('[{"stale": true}]\n', encoding="utf-8")
    count = compile_commands.generate(source.parent, database, ["make"], FakeRunner("cc -c source.c -o source.o\n"))
    assert count == 1
    assert json.loads(database.read_text(encoding="utf-8"))[0]["file"] == "source.c"
    assert not list(source.parent.glob(".compile_commands.json.*"))


def test_capture_does_not_execute_substitutions_in_noncompiler_recipes(source):
    marker = source.parent / "must-not-exist"
    runner = FakeRunner(f"echo `touch {marker}`\ncc -c source.c -o source.o\n")
    assert len(compile_commands.capture(source.parent, ["make"], runner)) == 1
    assert not marker.exists()


def test_compiler_substitutions_fail_instead_of_executing_or_dropping_commands(source):
    marker = source.parent / "must-not-exist"
    runner = FakeRunner(f"cc -I`touch {marker}` -c source.c -o source.o\n")
    with pytest.raises(compile_commands.CaptureError, match="requires shell evaluation"):
        compile_commands.capture(source.parent, ["make"], runner)
    assert not marker.exists()


def test_recursive_make_directories_are_preserved(tmp_path):
    nested = tmp_path / "bash-build"
    nested.mkdir()
    (nested / "shell.c").write_text("int shell;\n", encoding="utf-8")
    runner = FakeRunner(f"make[2]: Entering directory '{nested}'\ncc -c shell.c -o shell.o\nmake[2]: Leaving directory '{nested}'\n")
    entries = compile_commands.capture(tmp_path, ["make"], runner)
    assert entries[0]["directory"] == str(nested)
    assert (Path(entries[0]["directory"]) / entries[0]["file"]).is_file()


def test_inherited_silent_directory_flag_does_not_hide_recursive_paths(source):
    environment = {"MAKEFLAGS": "s --no-print-directory -- EXTRA_FLAGS=--no-print-directory", "CUSTOM": "preserved"}
    runner = FakeRunner("cc -c source.c -o source.o\n")
    compile_commands.capture(source.parent, ["make"], runner, environment)
    assert runner.environments == [{"MAKEFLAGS": "s  -- EXTRA_FLAGS=--no-print-directory", "CUSTOM": "preserved"}]
    assert environment["MAKEFLAGS"].startswith("s --no-print-directory")


@pytest.mark.parametrize("architecture", ["aarch64", "riscv64"])
def test_binary_discovery_excludes_removed_programs_without_retirement_filters(architecture):
    result = subprocess.run(
        ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", "-", "current-binaries",
         f"ARCH={architecture}"],
        input="current-binaries:\n\t@printf '%s\\n' $(USER_BINARY_DIRS)\n",
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    directories = set(result.stdout.splitlines())
    assert "user/services/hunde-io-host" in directories
    assert "user/commands/login" in directories
    assert "user/drivers/uart_rx" in directories
    assert "user/drivers/devfs" not in directories
    assert not (ROOT / "user/drivers/devfs/main.c").exists()
    assert "user/drivers/embedfs" not in directories
    assert not (ROOT / "user/drivers/embedfs/main.c").exists()
    for directory in ("user/commands/shell", "user/drivers/socket_router"):
        assert directory not in directories
        assert not (ROOT / directory).exists()
    assert "USER_RETIRED_BINARY_DIRS" not in (ROOT / "Makefile").read_text()


@pytest.mark.parametrize("architecture", ["aarch64", "riscv64"])
@pytest.mark.parametrize("artifact", ["object", "shared-library"])
def test_capture_prints_pic_compiler_commands_without_building_newlib(tmp_path, architecture, artifact):
    platform = tmp_path / architecture
    target = platform / ("user-pic/lib/hunde/block.o" if artifact == "object" else "sysroot/usr/lib/libc.wuff.1")
    result = subprocess.run(
        ["make", "-Bnw", "COMPILE_COMMANDS=1", f"ARCH={architecture}",
         f"PLATFORM_BUILD_ROOT={platform}", str(target)],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "-fPIC" in result.stdout
    assert "user/lib/hunde/block.c" in result.stdout
    assert "-idirafter" in result.stdout
    assert "newlib-pic/install" in result.stdout
    assert "all-target-newlib" not in result.stdout
    assert "install-target-newlib" not in result.stdout
    assert not result.stderr
    assert not platform.exists()


def test_bash_capture_does_not_regenerate_configuration_in_recursive_makes(tmp_path):
    probe = subprocess.run(
        ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", "-", "capture-flags", "COMPILE_COMMANDS=1"],
        input="capture-flags:\n\t@printf '%s\\n' $(BASH_CAPTURE_ARGUMENTS)\n",
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    assert probe.returncode == 0, probe.stderr
    for directory in [tmp_path, tmp_path / "child"]:
        directory.mkdir(exist_ok=True)
        recurse = "\nrecurse:\n\t$(MAKE) $(BASH_MAKEFLAGS) -C child bash\n" if directory == tmp_path else ""
        directory.joinpath("Makefile").write_text(
            "Makefile: config.status\n\t@touch regenerated\n"
            "config.status: configure\n\t@touch regenerated\n"
            "config.h: config.status\n\t@touch regenerated\n"
            "bash: source.o" + (" recurse\n" if recurse else "\n") +
            "source.o: source.c config.h\n\tcc -c source.c -o source.o\n" + recurse,
            encoding="utf-8",
        )
        for filename in ["config.status", "config.h", "configure", "source.c"]:
            directory.joinpath(filename).write_text("fixture\n", encoding="utf-8")
    result = subprocess.run(["make", "-Bnw", *probe.stdout.splitlines(), "bash"],
                            cwd=tmp_path, text=True, capture_output=True, check=False)
    assert result.returncode == 0, result.stderr
    assert result.stdout.count("cc -c source.c -o source.o") == 2
    assert not list(tmp_path.rglob("regenerated"))
    assert not list(tmp_path.rglob("source.o"))


@pytest.mark.parametrize("capture", [False, True])
def test_normal_build_retains_both_newlib_install_dependencies(capture):
    command = ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", "-", "dependency-probe"]
    if capture:
        command.append("COMPILE_COMMANDS=1")
    result = subprocess.run(
        command,
        input="dependency-probe:\n\t@printf '%s\\n' $(NEWLIB_INSTALL_PREREQUISITE) $(NEWLIB_PIC_INSTALL_PREREQUISITE)\n",
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    paths = result.stdout.split()
    if capture:
        assert paths == []
    else:
        assert len(paths) == 2
        assert paths[0].endswith("/newlib/install/.installed")
        assert paths[1].endswith("/newlib-pic/install/.installed")
