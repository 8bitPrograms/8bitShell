#!/usr/bin/env python3
"""Prepare a hash-locked private Microsoft ConPTY runtime for Windows x64.

An existing copy can be supplied with --source for offline builds. Only the
exact locked files are accepted, regardless of where they came from.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]

def prepare(output, source=None):
    lock = json.loads((ROOT / "packaging/conpty.lock.json").read_text())
    inventory = lock["architectures"]["x64"]
    output = Path(output)
    def valid(path, expected):
        return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == expected
    if all(valid(output / name, sha) for name, sha in inventory.items()):
        return output
    scratch = ROOT / "build"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bitshell-conpty-", dir=scratch) as tmp:
        work = Path(tmp)
        if source:
            source = Path(source)
        else:
            package = lock["package"].lower()
            version = lock["version"]
            url = f"https://api.nuget.org/v3-flatcontainer/{package}/{version}/{package}.{version}.nupkg"
            archive = work / "runtime.nupkg"
            with urllib.request.urlopen(url, timeout=60) as response, archive.open("wb") as stream:
                shutil.copyfileobj(response, stream)
            with zipfile.ZipFile(archive) as bundle:
                for name, expected in inventory.items():
                    matches = [member for member in bundle.namelist()
                               if Path(member).name == name and "x64" in member.lower()]
                    if len(matches) != 1:
                        raise ValueError(f"Expected one x64 {name} in the NuGet package")
                    (work / name).write_bytes(bundle.read(matches[0]))
            source = work
        # Check every file before copying any. No runtime downloads happen in
        # the application; the developer/package builder supplies this directory.
        for name, expected in inventory.items():
            if not valid(source / name, expected):
                raise ValueError(f"ConPTY SHA256 mismatch: {name}")
        output.mkdir(parents=True, exist_ok=True)
        for name in inventory:
            shutil.copy2(source / name, output / name)
        shutil.copy2(ROOT / "packaging/CONPTY-LICENSE.txt", output / "LICENSE.txt")
    return output

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "tooling/conpty-x64")
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    print(prepare(args.output, args.source))
