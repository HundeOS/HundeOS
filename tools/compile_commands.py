"""Capture checked Make dry runs and atomically publish every compiler variant."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import bashlex
from compiledb import generate_json_compdb
from compiledb.parser import Error as ParseError
from compiledb.parser import (
    SubstCommandVisitor, cc_compile_regex, cpp_compile_regex, compiler_wrappers,
    file_regex, make_enter_dir, make_leave_dir,
)


ROOT = Path(__file__).resolve().parents[1]


class CaptureError(Exception):
    pass


class CompilerCommands(bashlex.ast.nodevisitor):
    def __init__(self, line):
        self.line = line
        self.commands = []

    def visitcommand(self, node, parts):
        words = [part.word for part in parts if part.kind == "word"]
        if not words:
            return False
        executable = 0
        while executable < len(words) and words[executable] in compiler_wrappers:
            executable += 1
        if executable == len(words):
            return False
        compiler = words[executable]
        if not (cc_compile_regex.fullmatch(compiler) or cpp_compile_regex.fullmatch(compiler)):
            return False
        if not any(file_regex.fullmatch(word) for word in words[executable + 1:]):
            return False
        substitutions = SubstCommandVisitor()
        substitutions.visit(node)
        if substitutions.substs:
            raise CaptureError(f"Compiler command requires shell evaluation: {self.line}")
        self.commands.append(self.line[node.pos[0]:node.pos[1]])
        return False


def compiler_log(output):
    lines = iter(output.splitlines(keepends=True))
    for line in lines:
        while line.endswith("\\\n"):
            line = line[:-2] + next(lines, "")
        line = line.rstrip()
        if make_enter_dir.match(line) or make_leave_dir.match(line):
            yield line + "\n"
            continue
        try:
            trees = bashlex.parse(line)
        except (bashlex.errors.ParsingError, NotImplementedError):
            first = line.split(maxsplit=1)
            if first and (cc_compile_regex.fullmatch(first[0]) or cpp_compile_regex.fullmatch(first[0])
                          or first[0] in compiler_wrappers):
                raise CaptureError(f"Cannot parse compiler command: {line}") from None
            continue
        visitor = CompilerCommands(line)
        for tree in trees:
            visitor.visit(tree)
        for command in visitor.commands:
            yield command + "\n"


def capture(root, command, runner=subprocess.run, environment=None):
    environment = dict(os.environ if environment is None else environment)
    if "MAKEFLAGS" in environment:
        flags, separator, overrides = environment["MAKEFLAGS"].partition(" -- ")
        flags = re.sub(r"(?<!\S)--no-print-directory(?!\S)", "", flags)
        environment["MAKEFLAGS"] = flags + separator + overrides
    result = runner(command, cwd=root, text=True, capture_output=True, check=False, env=environment)
    if result.returncode != 0:
        raise CaptureError(f"Make capture failed ({result.returncode}):\n{result.stderr}")
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="")
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log:
        log.writelines(compiler_log(result.stdout))
        log.seek(0)
        entries = generate_json_compdb(log, proj_dir=str(root), use_full_path=True).compdb
    if not entries:
        raise CaptureError("Make capture produced no compiler commands")
    unique = {}
    for entry in entries:
        key = (entry["directory"], entry["file"], tuple(entry["arguments"]))
        unique.setdefault(key, entry)
    return list(unique.values())


def publish(output, entries):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output.parent,
                                         prefix=f".{output.name}.", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(entries, stream, indent=2)
            stream.write("\n")
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def generate(root, output, command, runner=subprocess.run):
    entries = capture(root, command, runner)
    publish(output, entries)
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    arguments = parser.parse_args()
    try:
        count = generate(ROOT, ROOT / "compile_commands.json",
                         [arguments.make, "--print-directory", "compile-commands-capture"])
    except (CaptureError, ParseError, OSError, ValueError) as error:
        print(f"compile-commands: {error}", file=sys.stderr)
        return 1
    print(f"compile-commands: {count} compiler variants recorded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
