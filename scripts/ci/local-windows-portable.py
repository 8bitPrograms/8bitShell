#!/usr/bin/env python3
"""Deploy an already tested Windows x64 build into a fresh local portable folder."""
import argparse
import os
from pathlib import Path
from distribution import ROOT, run, verify_binary
from build import crt_windows, openssh_windows, smoke

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This helper requires the Windows x64 MSVC environment.")
    stage = args.output.resolve()
    if stage.exists():
        parser.error("Choose a fresh output folder; existing packages and profiles are preserved.")
    run(["cmake", "--install", ROOT / "build/windows-release", "--prefix", stage])
    crt_windows(stage, "x64")
    openssh_windows(stage, "x64")
    (stage / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nPlugins=plugins\nQmlImports=qml\n")
    (stage / "Start.cmd").write_text('@echo off\nstart "" "%~dp0bin\\8bitShell.exe" %*\n')
    (stage / "Start-Portable.cmd").write_text('@echo off\nset "BIT8SHELL_PROFILE=%~dp0profile"\nstart "" "%~dp0bin\\8bitShell.exe" %*\n')
    verify_binary(stage / "bin/8bitShell.exe", "windows", "x64")
    smoke(stage, "windows")
    print(f"Portable folder ready; clean-environment startup passed: {stage}")

if __name__ == "__main__":
    main()
