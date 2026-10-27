#!/usr/bin/env python3
"""Build a SHA-256 release manifest; refuse missing or unexpected assets."""
import argparse
import hashlib
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    files = sorted(args.directory.iterdir())
    patterns = [r"bitshell-.+\.deb", r"bitshell-.+\.rpm", r"bitshell-.+-windows-x64\.zip",
                r"bitshell-.+-windows-x64\.msi", r"bitshell-.+-windows-x64-setup\.exe",
                r"bitshell-.+-linux-x86_64-portable\.tar\.gz", r"bitshell-.+-source\.tar\.gz"]
    for pattern in patterns:
        if len([path for path in files if re.fullmatch(pattern, path.name)]) != 1:
            raise ValueError(f"Expected exactly one asset matching {pattern}")
    lines = []
    for path in files:
        if path.is_symlink() or not path.is_file() or not any(re.fullmatch(p, path.name) for p in patterns):
            raise ValueError(f"Unexpected release asset: {path.name}")
        with path.open('rb') as source:
            digest = hashlib.file_digest(source, 'sha256').hexdigest()
        lines.append(f"{digest}  {path.name}\n")
    with (args.directory / 'SHA256SUMS').open('x', encoding='utf-8') as output:
        output.writelines(lines)


if __name__ == '__main__':
    main()
