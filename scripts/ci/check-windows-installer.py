#!/usr/bin/env python3
"""Read MSI metadata and verify the setup payload without installing the app."""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import tempfile

from distribution import ROOT, config, digest, verify_binary, version


def msi_metadata(path):
    msi = ctypes.WinDLL(str(Path(os.environ["SystemRoot"]) / "System32/msi.dll"))
    msi.MsiOpenDatabaseW.argtypes = (wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.POINTER(wintypes.UINT))
    msi.MsiDatabaseOpenViewW.argtypes = (wintypes.UINT, wintypes.LPCWSTR, ctypes.POINTER(wintypes.UINT))
    msi.MsiViewExecute.argtypes = (wintypes.UINT, wintypes.UINT)
    msi.MsiViewFetch.argtypes = (wintypes.UINT, ctypes.POINTER(wintypes.UINT))
    msi.MsiRecordGetStringW.argtypes = (wintypes.UINT, wintypes.UINT, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD))

    def check(code):
        if code:
            raise RuntimeError(f"MSI database operation failed: {code}")

    database = wintypes.UINT()
    check(msi.MsiOpenDatabaseW(str(path.resolve()), None, ctypes.byref(database)))

    def rows(query, columns):
        view = wintypes.UINT()
        check(msi.MsiDatabaseOpenViewW(database, query, ctypes.byref(view)))
        try:
            check(msi.MsiViewExecute(view, 0))
            while True:
                record = wintypes.UINT()
                code = msi.MsiViewFetch(view, ctypes.byref(record))
                if code == 259:  # ERROR_NO_MORE_ITEMS
                    break
                check(code)
                try:
                    fields = []
                    for column in range(1, columns + 1):
                        buffer = ctypes.create_unicode_buffer(32768)
                        size = wintypes.DWORD(len(buffer))
                        check(msi.MsiRecordGetStringW(record, column, buffer, ctypes.byref(size)))
                        fields.append(buffer.value)
                    yield fields
                finally:
                    msi.MsiCloseHandle(record)
        finally:
            msi.MsiViewClose(view)
            msi.MsiCloseHandle(view)

    try:
        properties = dict(rows("SELECT `Property`, `Value` FROM `Property`", 2))
        files = {row[0].split("|")[-1].lower() for row in rows("SELECT `FileName` FROM `File`", 1)}
        shortcuts = list(rows("SELECT `Target`, `Arguments` FROM `Shortcut`", 2))
        return properties, files, shortcuts
    finally:
        msi.MsiCloseHandle(database)


def verify(setup, package, architecture):
    verify_binary(setup, "windows", architecture)
    properties, files, shortcuts = msi_metadata(package)
    if properties.get("ProductVersion") != version():
        raise ValueError("MSI and application versions differ")
    if properties.get("Manufacturer") != config()["publisher"] or properties.get("ALLUSERS"):
        raise ValueError("Incorrect MSI publisher or per-user install scope")
    if properties.get("UpgradeCode", "").strip("{}").upper() != config()["upgradeCode"].upper():
        raise ValueError("MSI upgrade identity changed")
    required = {"8bitshell.exe", "qt6core.dll", "qt6quick.dll", "qwindows.dll",
                "puttygen.exe", "ssh.exe", "sftp.exe", "msvcp140.dll"}
    if architecture == "x64":
        required |= {"conpty.dll", "openconsole.exe"}
    if not required <= files:
        raise ValueError(f"Missing MSI payload: {sorted(required - files)}")
    if not any(target.lower().endswith("msiexec.exe") and arguments == "/x [ProductCode]"
               for target, arguments in shortcuts):
        raise ValueError("Missing product-specific uninstall shortcut")
    with tempfile.TemporaryDirectory(prefix="setup-check-", dir=ROOT / "build") as directory:
        # Exercise protected temporary directory creation/extraction/cleanup,
        # while explicitly avoiding the Windows Installer process.
        subprocess.run([str(setup.resolve()), "--verify"], check=True, timeout=30)
        extracted = Path(directory) / "payload.msi"
        subprocess.run([str(setup.resolve()), "--extract-msi", str(extracted)], check=True, timeout=30)
        if digest(extracted) != digest(package):
            raise ValueError("Setup embeds a different MSI")
        repeat = subprocess.run([str(setup.resolve()), "--extract-msi", str(extracted)], timeout=30)
        if repeat.returncode != 80 or digest(extracted) != digest(package):
            raise ValueError("Setup did not preserve an existing extraction target")
        invalid = subprocess.run([str(setup.resolve()), "/quiet", "/unsupported"], timeout=10)
        if invalid.returncode != 87:
            raise ValueError("Setup accepted an unsupported installer argument")
    print("MSI metadata/runtime checks passed; setup embeds the exact MSI and refuses overwrite/unsupported arguments.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--setup", type=Path, required=True)
    parser.add_argument("--msi", type=Path, required=True)
    parser.add_argument("--arch", choices=("x64", "arm64", "x86"), default="x64")
    args = parser.parse_args()
    verify(args.setup, args.msi, args.arch)
