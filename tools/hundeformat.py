#!/usr/bin/env python3
"""Run clang-format while preserving HundeOS match branches and lint annotations."""

import argparse
import re
import subprocess
import sys
from pathlib import Path

import hundelint


ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOTS = ("arch", "common", "common_includes", "kernel", "user", "test")
MATCH_MACROS = {
    "match_optional": (1, 2),
    "match_result": (1, 2),
    "match_find_result": (1, 2),
    "match_find_result_ptr": (1, 2),
}


def source_paths(root):
    for directory in SOURCE_ROOTS:
        for path in sorted((root / directory).rglob("*")):
            relative = path.relative_to(root)
            if path.suffix not in {".c", ".h"} or not path.is_file():
                continue
            if "build" in relative.parts or relative.is_relative_to("user/lib/newlib"):
                continue
            if path.name.endswith("_Runner.c") or relative == Path("user/drivers/video_term/emoji_atlas.h"):
                continue
            yield path


def preserve_conventions(source):
    clean = "\n".join(hundelint._sanitise_source(source))
    lines = []
    continued = False
    for line in clean.splitlines(keepends=True):
        directive = continued or line.lstrip().startswith("#")
        continued = directive and line.rstrip().endswith("\\")
        lines.append(re.sub(r"[^\n]", " ", line) if directive else line)
    clean = "".join(lines)
    edits = {}
    for macro, branch_indexes in MATCH_MACROS.items():
        for offset, args, end in hundelint._iter_macro_call_ranges(clean, macro):
            line_start = clean.rfind("\n", 0, offset) + 1
            prefix = clean[line_start:offset]
            if prefix.lstrip().startswith("#"):
                continue
            arguments = hundelint._split_macro_args_with_offsets(args)
            if len(arguments) != branch_indexes[-1] + 1:
                continue
            macro_indent = re.match(r"[ \t]*", prefix).group()
            indent = macro_indent + "    "
            opening = clean.index("(", offset, end)
            for index in branch_indexes:
                branch, relative = arguments[index]
                start = opening + 1 + relative
                whitespace = start
                while whitespace > 0 and source[whitespace - 1] in " \t":
                    whitespace -= 1
                if whitespace > 0 and source[whitespace - 1] != "\n":
                    edits[whitespace, start] = "\n" + indent
                first_line = branch.split("\n", 1)[0]
                if "\n" in branch and "{" in first_line:
                    brace = start + first_line.index("{")
                    whitespace = brace
                    while whitespace > start and source[whitespace - 1] in " \t":
                        whitespace -= 1
                    edits[whitespace, brace] = "\n" + indent
            closing = end - 1
            closing_line_start = clean.rfind("\n", 0, closing) + 1
            if clean[closing_line_start:closing].strip():
                edits[closing, closing] = "\n" + macro_indent
            else:
                edits[closing_line_start, closing] = macro_indent
    for (start, end), replacement in sorted(edits.items(), reverse=True):
        source = source[:start] + replacement + source[end:]

    return source


def format_source(source, path, command):
    for attempt in range(5):
        result = subprocess.run(
            [*command, "--style=file", f"--assume-filename={path}"],
            input=source, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
        )
        formatted = preserve_conventions(result.stdout)
        if formatted == source:
            return formatted
        source = formatted
    raise RuntimeError(f"formatter did not converge: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("provide a clang-format command after --")
    changed = 0
    count = 0
    for path in source_paths(ROOT):
        original = path.read_text()
        formatted = format_source(original, path, command)
        count += 1
        if formatted != original:
            changed += 1
            if args.check:
                print(f"{path.relative_to(ROOT)}: needs formatting")
            else:
                path.write_text(formatted)
    print(f"hundeformat: {count} files checked, {changed} {'need formatting' if args.check else 'formatted'}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"hundeformat: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        sys.exit(1)
