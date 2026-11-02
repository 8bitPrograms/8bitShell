#!/usr/bin/env python3
"""Collect committed application and verified Qt sources for a local release."""
import argparse
import json
import re
import subprocess
import tarfile
from pathlib import Path

from distribution import ROOT, config, digest, download, run, version

MODULES = ("qtbase", "qtshadertools", "qtsvg", "qt5compat",
           "qtlanguageserver", "qtdeclarative", "qttools", "qttranslations")


def qt_archives(cache, qt_version):
    archives = []
    for module in MODULES:
        archive = Path(cache) / f"{module}-everywhere-src-{qt_version}.tar.xz"
        checksum = archive.with_name(archive.name + ".sha256")
        if not archive.is_file() or not checksum.is_file():
            raise ValueError(f"Missing corresponding Qt source/cache checksum: {archive.name}")
        words = checksum.read_text().split()
        expected = words[0].lower() if words else ""
        if not re.fullmatch(r"[0-9a-f]{64}", expected) or digest(archive) != expected:
            raise ValueError(f"Qt source checksum mismatch: {archive.name}")
        archives.extend((archive, checksum))
    return archives


def collect(out, qt_root, cache, fetch=False):
    c, v = config(), version()
    header = Path(qt_root) / "include/QtCore/qconfig.h"
    if not re.search(r'#define\s+QT_VERSION_STR\s+"' + re.escape(c["qtVersion"]) + '"', header.read_text()):
        raise ValueError("Release configuration and Qt SDK version differ")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT).strip():
        raise ValueError("Commit reviewed changes before archiving release sources")
    if fetch:
        qt_version = c["qtVersion"]
        for module in MODULES:
            name = f"{module}-everywhere-src-{qt_version}.tar.xz"
            url = f"https://download.qt.io/archive/qt/{qt_version.rsplit('.', 1)[0]}/{qt_version}/submodules/{name}"
            checksum = download(url + ".sha256", Path(cache) / (name + ".sha256"))
            words = checksum.read_text().split()
            if not words or not re.fullmatch(r"[0-9a-fA-F]{64}", words[0]):
                raise ValueError(f"Invalid Qt source checksum: {name}")
            download(url, Path(cache) / name, words[0].lower())
    archives = qt_archives(cache, c["qtVersion"])
    out = Path(out)
    project = out / f'{c["app"]}-{v}-source.tar.gz'
    qt = out / f'{c["app"]}-{v}-qt-{c["qtVersion"]}-source.tar.gz'
    manifest = out / "manifest-source-windows-x64.json"
    if any(p.exists() for p in (project, qt, manifest)):
        raise ValueError("Source output already exists; choose a fresh release folder")
    out.mkdir(parents=True, exist_ok=True)
    run(["git", "archive", "--format=tar.gz", f'--prefix={c["app"]}-{v}-source/',
         "HEAD", "-o", project.resolve()], cwd=ROOT)
    with tarfile.open(qt, "w:gz") as bundle:
        for archive in archives:
            bundle.add(archive, arcname=archive.name)
    entries = [{"name": p.name, "sha256": digest(p), "size": p.stat().st_size,
                "os": "source", "arch": "all", "version": v,
                "glibc": None, "experimental": False} for p in (project, qt)]
    manifest.write_text(json.dumps(entries, indent=2) + "\n")
    print(f"Corresponding source bundles ready: {out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--qt-root", type=Path, default=ROOT / "build/tooling/qt-sdk")
    parser.add_argument("--qt-sources", type=Path, default=ROOT / "tooling/qt-sources")
    parser.add_argument("--fetch", action="store_true", help="Populate the cache from official Qt archives/checksums")
    args = parser.parse_args()
    collect(args.out, args.qt_root, args.qt_sources, args.fetch)
