#!/usr/bin/env python3
import subprocess
from pathlib import Path

EMOJI_SIZE = 32
ASSET_DIR = Path("vendor/noto_emoji/png/128")
HEADER = Path("user/drivers/video_term/emoji_atlas.h")
OUTPUT = Path("build/user/drivers/video_term/emoji_atlas.rgba")


def parse_codepoints(path):
    name = path.stem
    if not name.startswith("emoji_u"):
        raise ValueError(f"bad emoji asset name: {path}")
    return [int(part, 16) for part in name[len("emoji_u"):].split("_")]


def entry_name(codepoints):
    return "_".join(f"{codepoint:x}" for codepoint in codepoints)


def write_header(entries):
    lines = [
        "#ifndef __USER_DRIVERS_VIDEO_TERM_EMOJI_ATLAS_H",
        "#define __USER_DRIVERS_VIDEO_TERM_EMOJI_ATLAS_H",
        "",
        "#include <stdint.h>",
        "",
        "#define VIDEO_TERM_EMOJI_SIZE 32ULL",
        "#define VIDEO_TERM_EMOJI_MAX_CODEPOINTS 12ULL",
        "#define VIDEO_TERM_EMOJI_ADVANCE_COLUMNS 4ULL",
        "",
        "typedef struct VideoTermEmojiEntry",
        "{",
        "    uint32_t codepoints[VIDEO_TERM_EMOJI_MAX_CODEPOINTS];",
        "    uint8_t codepoint_count;",
        "    uint32_t atlas_offset;",
        "}",
        "VideoTermEmojiEntry;",
        "",
        "typedef const VideoTermEmojiEntry * VideoTermEmojiEntryPtr;",
        "",
        "static const VideoTermEmojiEntry VIDEO_TERM_EMOJI_ENTRIES[] = {",
    ]

    for codepoints, offset in entries:
        padded = [*codepoints, *([0] * (12 - len(codepoints)))]
        values = ", ".join(f"0x{codepoint:x}" for codepoint in padded)
        lines.append(
            f"    /* {entry_name(codepoints)} */ "
            f"{{ .codepoints = {{ {values} }}, .codepoint_count = {len(codepoints)}, .atlas_offset = {offset} }},"
        )

    lines.extend([
        "};",
        "",
        "static const uint64_t VIDEO_TERM_EMOJI_ENTRY_COUNT =",
        "    sizeof(VIDEO_TERM_EMOJI_ENTRIES) / sizeof(VIDEO_TERM_EMOJI_ENTRIES[0]);",
        "",
        "#endif",
        "",
    ])

    HEADER.write_text("\n".join(lines))


def main():
    assets = sorted(ASSET_DIR.glob("emoji_u*.png"))
    if not assets:
        raise SystemExit(f"no emoji assets found in {ASSET_DIR}")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    entries = []
    with OUTPUT.open("wb") as output:
        for source in assets:
            codepoints = parse_codepoints(source)
            offset = output.tell()
            rgba = subprocess.check_output([
                "magick",
                str(source),
                "-resize",
                f"{EMOJI_SIZE}x{EMOJI_SIZE}",
                "-background",
                "none",
                "-gravity",
                "center",
                "-extent",
                f"{EMOJI_SIZE}x{EMOJI_SIZE}",
                "rgba:-",
            ])
            expected = EMOJI_SIZE * EMOJI_SIZE * 4
            if len(rgba) != expected:
                raise SystemExit(f"bad RGBA size for {source}: {len(rgba)} != {expected}")
            output.write(rgba)
            entries.append((codepoints, offset))
            if len(codepoints) == 1 and codepoints[0] != 0xfe0f:
                entries.append(([codepoints[0], 0xfe0f], offset))

    entries.sort(key=lambda entry: (entry[0], entry[1]))
    write_header(entries)


if __name__ == "__main__":
    main()
