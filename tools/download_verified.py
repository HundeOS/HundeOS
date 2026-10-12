#!/usr/bin/env python3
import argparse
import hashlib
import os
from pathlib import Path
import urllib.request


COPY_SIZE = 1024 * 1024


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(COPY_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def download_verified(url: str, expected_sha256: str, output: Path) -> None:
    expected = expected_sha256.lower()
    if output.exists() and sha256(output) == expected:
        return

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with urllib.request.urlopen(url) as source, temporary.open("wb") as target:
            while chunk := source.read(COPY_SIZE):
                target.write(chunk)
        actual = sha256(temporary)
        if actual != expected:
            raise RuntimeError(f"SHA-256 mismatch for {url}: expected {expected}, got {actual}")
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download a file and require an exact SHA-256 digest.")
    parser.add_argument("url")
    parser.add_argument("sha256")
    parser.add_argument("output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    download_verified(args.url, args.sha256, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
