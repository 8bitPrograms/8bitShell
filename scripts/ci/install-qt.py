#!/usr/bin/env python3
"""Install the locked official Qt SDK into a NEW directory; no Qt login required.

The reviewed SHA-256 lock works with Qt 6.11's architecture-split repository.
Neither TLS verification nor archive integrity checks are optional.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parents[2]


class HttpsOnly(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.startswith("https://"):
            raise ValueError("Refusing a non-HTTPS redirect")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("linux-x64", "windows-x64"), required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()
    prefix = args.prefix.absolute()
    if prefix.exists():
        raise ValueError("Choose a new SDK prefix; never overwrite an installed SDK")
    sevenzip = shutil.which("7z") or shutil.which("7zz")
    if not sevenzip and os.name == "nt":
        candidate = Path(os.environ["ProgramFiles"]) / "7-Zip/7z.exe"
        if candidate.is_file():
            sevenzip = str(candidate)
    if not sevenzip:
        raise ValueError("Install 7-Zip through your OS package manager")
    lock = json.loads((ROOT / "packaging/qt-sdk.lock.json").read_text(encoding="utf-8"))
    opener = urllib.request.build_opener(HttpsOnly())
    prefix.mkdir(parents=True)
    sdk_prefix = prefix
    for record in lock[args.platform]:
        # The official ICU archive has a flat layout; Qt libraries/tools look
        # beside libQt6Core, not in the SDK root. Do not use LD_LIBRARY_PATH.
        prefix = sdk_prefix / 'lib' if record['module'] == 'icu' else sdk_prefix
        url, expected = record["url"], record["sha256"]
        if not url.startswith("https://download.qt.io/online/qtsdkrepository/") or not re.fullmatch("[0-9a-f]{64}", expected):
            raise ValueError("Invalid Qt SDK lock entry")
        print(f"Fetching locked Qt {lock['qtVersion']}: {record['module']}", flush=True)
        with tempfile.TemporaryDirectory(prefix="bitshell-qt-") as temporary:
            archive = Path(temporary) / "module.7z"
            digest = hashlib.sha256()
            length = 0
            with opener.open(url, timeout=120) as response, archive.open("xb") as output:
                while chunk := response.read(1024 * 1024):
                    length += len(chunk)
                    if length > 1024 * 1024 * 1024:
                        raise ValueError("SDK archive exceeds the 1 GiB bound")
                    digest.update(chunk)
                    output.write(chunk)
            if digest.hexdigest() != expected:
                raise ValueError(f"SHA-256 mismatch for {record['module']}")
            listing = subprocess.run([sevenzip, "l", "-slt", "-ba", str(archive)],
                                     check=True, capture_output=True, text=True).stdout
            entries = [dict(line.split(" = ", 1) for line in block.splitlines() if " = " in line)
                       for block in listing.strip().split("\n\n")]
            links = []
            for entry in entries:
                name = entry.get("Path", "").replace("\\", "/")
                path = PurePosixPath(name)
                if not name or path.is_absolute() or ".." in path.parts or ":" in name or "\n" in name or "\r" in name:
                    raise ValueError("SDK archive entry escapes its installation prefix")
                if not (prefix / name).resolve().is_relative_to(prefix.resolve()):
                    raise ValueError("SDK entry follows a link outside the prefix")
                if " l" in entry.get("Attributes", ""):
                    if int(entry.get("Size", "0")) not in range(1, 4097):
                        raise ValueError("Unexpected SDK symbolic link size")
                    links.append(entry)
            exclusions = []
            link_targets = []
            if links:
                # Never disable 7-Zip's dangerous-link protection. Read link
                # payloads without filesystem extraction, validate them, then
                # extract ordinary files and create only in-prefix links.
                link_list = Path(temporary) / "links.txt"
                link_list.write_text("\n".join(item["Path"] for item in links) + "\n", encoding="utf-8")
                raw = subprocess.run([sevenzip, "e", "-so", "-spd", str(archive), f"@{link_list}"],
                                     check=True, capture_output=True).stdout
                for entry in links:
                    size = int(entry["Size"])
                    payload, raw = raw[:size], raw[size:]
                    if f"{zlib.crc32(payload):08X}" != entry["CRC"]:
                        raise ValueError("7-Zip link stream order/CRC mismatch")
                    target = payload.decode("utf-8")
                    link = prefix / entry["Path"]
                    if PurePosixPath(target).is_absolute() or ":" in target or "\\" in target or "\0" in target:
                        raise ValueError("Unsafe SDK symbolic link")
                    if not (link.parent / target).resolve().is_relative_to(prefix.resolve()):
                        raise ValueError("SDK symbolic link escapes the prefix")
                    link_targets.append((link, target))
                if raw:
                    raise ValueError("Unexpected extra symbolic link payload")
                exclusions = [f"-x@{link_list}", "-spd"]
            subprocess.run([sevenzip, "x", "-y", *exclusions, f"-o{prefix}", str(archive)],
                           check=True, stdout=subprocess.DEVNULL)
            for link, target in link_targets:
                if link.is_symlink() and os.readlink(link) == target:
                    continue
                if link.exists() or link.is_symlink():
                    raise ValueError("SDK symbolic link collides with an existing file")
                link.parent.mkdir(parents=True, exist_ok=True)
                link.symlink_to(target)
    # Qt's official archives can be relocated using a relative qt.conf. Only the
    # SDK's build tools use this file; deployed applications receive their own.
    (sdk_prefix / "bin/qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="utf-8")
    print(f"Qt SDK ready: {sdk_prefix}")


if __name__ == "__main__":
    main()
