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
MULTILINE_MACROS = {
    "match_optional": (1, 2),
    "match_result": (1, 2),
    "match_find_result": (1, 2),
    "match_find_result_ptr": (1, 2),
    "critical_section": (0, 1),
    "critical_section_try": (0, 1, 2),
    "for_each_ll": (0, 1, 2, 3),
    "for_each_ll_ptr": (0, 1, 2, 3),
    "for_each_fa": (0, 1, 2, 3, 4),
    "for_each_fa_ptr": (0, 1, 2, 3, 4),
    "for_each_atomic_fa_ptr": (0, 1, 2, 3, 4),
    "for_each_map": (0, 1, 2, 3, 4, 5),
    "for_each_map_ptr": (0, 1, 2, 3, 4, 5),
    "for_each_hashtable": (0, 1, 2, 3, 4, 5, 6),
    "for_each_hashtable_ptr": (0, 1, 2, 3, 4, 5, 6),
    "for_each_ring_buffer": (0, 1, 2, 3, 4),
    "for_each_ring_buffer_ptr": (0, 1, 2, 3, 4),
    "for_each_bstring": (0, 1, 2, 3),
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


def formatting_source(source):
    clean = "\n".join(hundelint._sanitise_source(source))
    lines = []
    continued = False
    for line in clean.splitlines(keepends=True):
        directive = continued or line.lstrip().startswith("#")
        continued = directive and line.rstrip().endswith("\\")
        lines.append(re.sub(r"[^\n]", " ", line) if directive else line)
    return "".join(lines)


def preserve_conventions(source):
    clean = formatting_source(source)
    edits = {}
    for macro, branch_indexes in MULTILINE_MACROS.items():
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
                line_end = source.find("\n", start)
                if line_end == -1:
                    line_end = len(source)
                if "hundelint:" in source[start:line_end]:
                    continue
                first_line = branch.split("\n", 1)[0]
                if "{" in first_line and not branch.startswith("{"):
                    brace = start + first_line.index("{")
                    whitespace = brace
                    while whitespace > start and source[whitespace - 1] in " \t":
                        whitespace -= 1
                    edits[whitespace, brace] = "\n" + indent
                if macro.startswith("match_") and "{" in branch and branch.endswith("}"):
                    brace = start + branch.index("{")
                    closing_brace = start + len(branch) - 1
                    body_start = brace + 1
                    while body_start < closing_brace and source[body_start].isspace():
                        body_start += 1
                    if body_start < closing_brace:
                        edits[brace + 1, body_start] = "\n" + indent + "    "
                    else:
                        edits[brace + 1, closing_brace] = "\n" + indent
                        continue
                    body_end = closing_brace
                    while body_end > body_start and source[body_end - 1].isspace():
                        body_end -= 1
                    edits[body_end, closing_brace] = "\n" + indent
            closing = end - 1
            closing_line_start = clean.rfind("\n", 0, closing) + 1
            if clean[closing_line_start:closing].strip():
                edits[closing, closing] = "\n" + macro_indent
            else:
                edits[closing_line_start, closing] = macro_indent
    for (start, end), replacement in sorted(edits.items(), reverse=True):
        source = source[:start] + replacement + source[end:]

    return align_macro_blocks(source)


def align_macro_blocks(source):
    clean = formatting_source(source)
    lines = source.splitlines(keepends=True)
    clean_lines = clean.splitlines()
    calls = sorted(
        call
        for macro in MULTILINE_MACROS
        if not macro.startswith("match_")
        for call in hundelint._iter_macro_call_ranges(clean, macro)
    )
    # Align outer blocks first so nested macros inherit the corrected indentation.
    for offset, args, end in calls:
        macro_line = source.count("\n", 0, offset)
        if lines[macro_line].lstrip().startswith("#"):
            continue
        indent = len(lines[macro_line]) - len(lines[macro_line].lstrip()) + 4
        opening = clean.index("(", offset, end)
        for argument, relative in hundelint._split_macro_args_with_offsets(args):
            if not argument.startswith("{"):
                continue
            first = source.count("\n", 0, opening + 1 + relative)
            last = first + argument.count("\n")
            current = len(lines[first]) - len(lines[first].lstrip())
            delta = indent - current
            for index in range(first, last + 1):
                if not clean_lines[index].strip():
                    continue
                if delta >= 0:
                    lines[index] = " " * delta + lines[index]
                else:
                    removable = len(lines[index]) - len(lines[index].lstrip(" \t"))
                    lines[index] = lines[index][min(-delta, removable):]
    return "".join(lines)


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
