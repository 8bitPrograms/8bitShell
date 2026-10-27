#!/usr/bin/env python3
"""Build and verify the locked command-line-only PuTTYgen helper."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
MAXIMUM_DOWNLOAD = 16 * 1024 * 1024
MAXIMUM_EXTRACTED = 128 * 1024 * 1024
MAXIMUM_MEMBERS = 20_000


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def provenance_path(executable: Path) -> Path:
    return Path(f"{executable}.provenance.json")


def platform_id() -> str:
    machine = (os.environ.get("RELEASE_ARCH") or platform.machine()).lower()
    arch = {"x64": "x64", "armv7": "armv7", "amd64": "x64", "x86_64": "x64", "arm64": "arm64", "aarch64": "arm64",
            "x86": "x86", "i686": "x86", "armv7l": "armv7"}.get(machine)
    if not arch: raise SystemExit(f"Unsupported PuTTYgen build architecture: {machine}")
    system = "windows" if os.name == "nt" else ("macos" if platform.system() == "Darwin" else "linux")
    # Preserve the recorded x64 Linux identifier used by existing verified binaries.
    return f"{system}-" + ("x86_64" if system == "linux" and arch == "x64" else arch)


def verify_output(executable: Path, lock: dict) -> dict:
    metadata_file = provenance_path(executable)
    if not executable.is_file() or not metadata_file.is_file():
        raise SystemExit("PuTTYgen executable or provenance metadata is missing.")
    metadata = json.loads(metadata_file.read_text(encoding="utf-8"))
    expected = {
        "schemaVersion": 1,
        "kind": "puttygen-command-line",
        "version": lock["version"],
        "sourceUrl": lock["source"]["url"],
        "sourceSha256": lock["source"]["sha256"],
        "platform": platform_id(),
    }
    for key, value in expected.items():
        if metadata.get(key) != value:
            raise SystemExit(f"PuTTYgen provenance field {key} does not match the lock.")
    actual = sha256(executable)
    if metadata.get("binarySha256") != actual:
        raise SystemExit("PuTTYgen binary does not match its provenance metadata.")
    version = subprocess.run(
        [str(executable), "--version"], check=True, capture_output=True, text=True
    ).stdout
    if f"puttygen: Release {lock['version']}" not in version:
        raise SystemExit("Built PuTTYgen version does not match the lock.")
    return metadata


def add_windows_cli_target(source: Path) -> None:
    adapter = ROOT / "scripts/ci/puttygen-windows-random.c"
    fragment = ROOT / "scripts/ci/puttygen-windows-cli.cmake"
    shutil.copy2(adapter, source / "windows/bitshell-puttygen-random.c")
    cmake_file = source / "windows/CMakeLists.txt"
    text = cmake_file.read_text(encoding="utf-8")
    marker = "installed_program(puttygen)\n"
    if text.count(marker) != 1:
        raise SystemExit("PuTTY Windows build layout no longer matches the reviewed CLI injection point.")
    injected = marker + "\n" + fragment.read_text(encoding="utf-8").rstrip() + "\n"
    cmake_file.write_text(text.replace(marker, injected), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path)
    mode.add_argument("--verify", type=Path)
    args = parser.parse_args()
    lock = json.loads((ROOT / "packaging/puttygen.lock.json").read_text())

    if args.verify:
        print(json.dumps(verify_output(args.verify.resolve(), lock), indent=2))
        return 0

    output = args.output.resolve()
    metadata_file = provenance_path(output)
    if output.exists() or metadata_file.exists():
        raise SystemExit(f"Refusing to replace existing output or metadata: {output}")

    artifact = lock["source"]
    parsed = urllib.parse.urlparse(artifact["url"])
    if parsed.scheme != "https" or parsed.hostname != "the.earth.li":
        raise SystemExit("The PuTTY source lock does not use the trusted HTTPS origin.")

    with tempfile.TemporaryDirectory(prefix="bitshell-puttygen-") as temporary:
        temporary_path = Path(temporary)
        archive = temporary_path / "putty.tar.gz"
        request = urllib.request.Request(
            artifact["url"], headers={"User-Agent": "8bitShell release builder"}
        )
        downloaded = 0
        with urllib.request.urlopen(request, timeout=60) as response, archive.open("wb") as destination:
            while chunk := response.read(1024 * 1024):
                downloaded += len(chunk)
                if downloaded > MAXIMUM_DOWNLOAD:
                    raise SystemExit("PuTTY source exceeds the download limit.")
                destination.write(chunk)
        actual = sha256(archive)
        if actual != artifact["sha256"]:
            raise SystemExit("PuTTY source SHA-256 verification failed.")

        source_parent = temporary_path / "source"
        source_parent.mkdir()
        with tarfile.open(archive, "r:gz") as bundle:
            members = bundle.getmembers()
            if len(members) > MAXIMUM_MEMBERS or sum(member.size for member in members) > MAXIMUM_EXTRACTED:
                raise SystemExit("PuTTY source archive exceeds extraction limits.")
            bundle.extractall(source_parent, members=members, filter="data")
        roots = [entry for entry in source_parent.iterdir() if entry.is_dir()]
        if len(roots) != 1 or roots[0].name != f"putty-{lock['version']}":
            raise SystemExit("PuTTY source archive has an unexpected layout.")

        windows = os.name == "nt"
        if windows:
            add_windows_cli_target(roots[0])
        build = temporary_path / "build"
        configure = [
            "cmake", "-S", str(roots[0]), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
        ]
        if not windows:
            configure.extend(["-DPUTTY_GTK_VERSION=NONE"])
        subprocess.run(configure, check=True)
        target = "bitshell-puttygen-cli" if windows else "puttygen"
        subprocess.run(
            ["cmake", "--build", str(build), "--target", target, "--parallel", "2"],
            check=True,
        )
        built = build / ("bitshell-puttygen-cli.exe" if windows else "puttygen")
        version = subprocess.run(
            [str(built), "--version"], check=True, capture_output=True, text=True
        ).stdout
        if f"Release {lock['version']}" not in version:
            raise SystemExit("Built PuTTYgen version does not match the lock.")
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built, output)
        output.chmod(0o755)
        metadata = {
            "schemaVersion": 1,
            "kind": "puttygen-command-line",
            "version": lock["version"],
            "sourceUrl": artifact["url"],
            "sourceSha256": artifact["sha256"],
            "platform": platform_id(),
            "binarySha256": sha256(output),
        }
        metadata_file.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        verify_output(output, lock)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
