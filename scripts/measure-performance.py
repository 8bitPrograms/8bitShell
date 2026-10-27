#!/usr/bin/env python3
"""Measure a rebuilt Windows 8bitShell with isolated local sessions, no network.

Examples:
  python scripts/measure-performance.py --binary build/windows-release/bitshell.exe \
    --fixture build/windows-release/windows_terminal_fixture.exe --output build/performance-idle.json
  python scripts/measure-performance.py --binary ... --fixture ... --workload history --output ...

Offscreen/software is the default. Use --native-window --renderer default for
actual display/backend costs. Working set and private commit are separate; GPU
allocations are outside this probe. A running process is not an SSH-ready prompt.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
import time


class MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("page_faults", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "peak_working_set", "working_set", "quota_peak_paged", "quota_paged",
            "quota_peak_nonpaged", "quota_nonpaged", "pagefile", "peak_pagefile", "private")]


class ProcessEntry(ctypes.Structure):
    _fields_ = [
        ("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
        ("default_heap", ctypes.c_size_t), ("module_id", wintypes.DWORD), ("threads", wintypes.DWORD),
        ("parent_pid", wintypes.DWORD), ("priority", wintypes.LONG), ("flags", wintypes.DWORD),
        ("executable", wintypes.WCHAR * 260)]


class WindowsCounters:
    def __init__(self):
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "OpenProcess": ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
            "K32GetProcessMemoryInfo": ([wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD], wintypes.BOOL),
            "GetProcessTimes": ([wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4, wintypes.BOOL),
            "CreateToolhelp32Snapshot": ([wintypes.DWORD, wintypes.DWORD], wintypes.HANDLE),
            "Process32FirstW": ([wintypes.HANDLE, ctypes.POINTER(ProcessEntry)], wintypes.BOOL),
            "Process32NextW": ([wintypes.HANDLE, ctypes.POINTER(ProcessEntry)], wintypes.BOOL),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = arguments, result

    def process(self, pid):
        handle = self.api.OpenProcess(0x0400 | 0x0010, False, pid)
        if not handle:
            return None
        try:
            counters = MemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            if not self.api.K32GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
                return None
            created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
            if not self.api.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(exited),
                                            ctypes.byref(kernel), ctypes.byref(user)):
                return None
            cpu = sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in (kernel, user)) / 1e7
            return {"private_bytes": counters.private, "working_set_bytes": counters.working_set,
                    "peak_working_set_bytes": counters.peak_working_set, "cpu_seconds": cpu}
        finally:
            self.api.CloseHandle(handle)

    def descendants(self, root):
        snapshot = self.api.CreateToolhelp32Snapshot(0x00000002, 0)
        if snapshot == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_last_error(), "Cannot enumerate process descendants")
        parents = {}
        try:
            entry = ProcessEntry()
            entry.size = ctypes.sizeof(entry)
            present = self.api.Process32FirstW(snapshot, ctypes.byref(entry))
            if not present:
                raise OSError(ctypes.get_last_error(), "Cannot read process snapshot")
            while present:
                parents[entry.pid] = entry.parent_pid
                present = self.api.Process32NextW(snapshot, ctypes.byref(entry))
        finally:
            self.api.CloseHandle(snapshot)
        found = {root}
        while True:
            expanded = found | {pid for pid, parent in parents.items() if parent in found}
            if expanded == found:
                return found
            found = expanded


def measure_case(counters, binary, fixture, tabs, args):
    work_root = args.output.parent.resolve()
    work_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bitshell-performance-", dir=work_root) as temporary:
        directory = Path(temporary).resolve()
        if not directory.is_relative_to(work_root):
            raise ValueError("Temporary probe directory escaped its output directory")
        profile = directory / "config" / "bitshell"
        profile.mkdir(parents=True)
        (profile / "settings.yaml").write_text(
            "version: 1\nkind: bitshell/settings\nlogging:\n  enabledByDefault: false\n", encoding="utf-8")
        env = os.environ.copy()
        env["XDG_CONFIG_HOME"] = str(directory / "config")
        env["XDG_DATA_HOME"] = str(directory / "data")
        if not args.native_window:
            env["QT_QPA_PLATFORM"] = "offscreen"
        else:
            env.pop("QT_QPA_PLATFORM", None)
        if args.renderer == "software":
            env["QT_QUICK_BACKEND"] = "software"
        else:
            env.pop("QT_QUICK_BACKEND", None)
        report_path = directory / "startup.json"
        command = [str(binary), "--performance-report", str(report_path), "--performance-tabs", str(tabs),
                   "--performance-duration", str(round(args.seconds * 1000)), "--window-size", args.window_size,
                   "-e", str(fixture), "history" if args.workload == "history" else "sleep"]
        samples, trees = [], []
        start = time.perf_counter()
        process = subprocess.Popen(command, env=env, cwd=directory, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            last_tree = -1.0
            while process.poll() is None:
                now = time.perf_counter() - start
                if now > args.seconds + 30:
                    raise TimeoutError("8bitShell exceeded the performance probe timeout")
                sample = counters.process(process.pid)
                if sample:
                    samples.append({"time": now, **sample})
                if now - last_tree >= 0.5:
                    pids = counters.descendants(process.pid)
                    values = [counters.process(pid) for pid in pids]
                    complete = all(value is not None for value in values)
                    trees.append({"time": now, "processes": len(pids), "complete": complete,
                                  "private_bytes": sum(value["private_bytes"] for value in values if value),
                                  "working_set_bytes": sum(value["working_set_bytes"] for value in values if value)})
                    last_tree = now
                time.sleep(0.05)
            _, stderr = process.communicate(timeout=5)
            if process.returncode or not report_path.is_file():
                detail = stderr.decode("utf-8", errors="replace")[-1200:]
                raise RuntimeError(f"Rebuilt 8bitShell probe failed (exit {process.returncode}): {detail}")
            report = json.loads(report_path.read_text(encoding="utf-8"))
            if report["tabs"] != tabs or report["running_local_processes"] != tabs:
                raise RuntimeError("Probe did not start the requested local fixture processes")
            if len(report["sessions"]) != tabs or any(session["logging_enabled"] for session in report["sessions"]):
                raise RuntimeError("Probe session diagnostics are incomplete or logging was not disabled")
            if args.workload == "history" and any(not session["fixture_output_complete"] for session in report["sessions"]):
                raise RuntimeError("History fixture output is still loading; increase --seconds for this workload")
            if not samples:
                raise RuntimeError("Windows refused process memory measurements")
            steady = [sample for sample in samples if sample["time"] >= samples[-1]["time"] - 1]
            span = steady[-1]["time"] - steady[0]["time"]
            cpu = 100 * (steady[-1]["cpu_seconds"] - steady[0]["cpu_seconds"]) / span if span else None
            tail = trees[-2:]
            return {"tabs": tabs, "startup": report,
                    "steady_private_bytes": statistics.median(sample["private_bytes"] for sample in steady),
                    "steady_working_set_bytes": statistics.median(sample["working_set_bytes"] for sample in steady),
                    "sampled_peak_private_bytes": max(sample["private_bytes"] for sample in samples),
                    "peak_working_set_bytes": max(sample["peak_working_set_bytes"] for sample in samples),
                    "steady_cpu_percent_of_one_core": cpu,
                    "steady_tree_private_bytes": statistics.median(sample["private_bytes"] for sample in tail),
                    "steady_tree_working_set_bytes": statistics.median(sample["working_set_bytes"] for sample in tail),
                    "process_tree_complete": all(sample["complete"] for sample in tail),
                    "sampled_peak_process_count": max(sample["processes"] for sample in trees)}
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--tabs", type=int, nargs="+", default=[0, 1, 5, 20])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--seconds", type=float, default=3)
    parser.add_argument("--window-size", default="1280x760")
    parser.add_argument("--renderer", choices=["software", "default"], default="software")
    parser.add_argument("--workload", choices=["idle", "history"], default="idle")
    parser.add_argument("--native-window", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This process-memory probe uses Windows counters")
    counters = WindowsCounters()
    if args.self_test:
        result = counters.process(os.getpid())
        if not result or min(result["private_bytes"], result["working_set_bytes"]) <= 0:
            raise RuntimeError("Process counters unavailable")
        if os.getpid() not in counters.descendants(os.getpid()):
            raise RuntimeError("Process snapshot unavailable")
        print("PASS: 64-bit process memory, CPU counters, and descendant enumeration")
        return
    if not all((args.binary, args.fixture, args.output)):
        parser.error("--binary, --fixture, and --output are required")
    if any(tabs < 0 or tabs > 20 for tabs in args.tabs) or not 1 <= args.repeats <= 20 or not 0.5 <= args.seconds <= 30:
        parser.error("Use 0..20 tabs, 1..20 repeats, and 0.5..30 seconds")
    # GetFinalPathNameByHandle on newly linked Windows executables can be denied
    # in a restricted host even when reading/running the same files is allowed.
    # These are explicit user-supplied executables, not untrusted terminal paths.
    binary, fixture = args.binary.absolute(), args.fixture.absolute()
    if not binary.is_file() or not fixture.is_file():
        parser.error("The executable or local fixture does not exist")
    binary_stamp = binary.stat()
    fixture_stamp = fixture.stat()
    cases = []
    for tabs in args.tabs:
        for repeat in range(args.repeats):
            print(f"Measuring {tabs} local tabs, {args.workload}, repeat {repeat + 1}", flush=True)
            cases.append({"repeat": repeat + 1, **measure_case(counters, binary, fixture, tabs, args)})
    with binary.open("rb") as stream:
        binary_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    with fixture.open("rb") as stream:
        fixture_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    for path, before in ((binary, binary_stamp), (fixture, fixture_stamp)):
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise RuntimeError("The measured executable changed during the probe; rerun on one immutable build")
    output = {"schema": 1, "measured_at": datetime.now(timezone.utc).isoformat(),
              "binary_sha256": binary_hash, "fixture_sha256": fixture_hash,
              "display": "native" if args.native_window else "offscreen", "renderer": args.renderer,
              "workload": args.workload, "gpu_memory_included": False,
              "scope": "main process and enumerated descendants; no network or personal profile", "cases": cases}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {args.output}")


if __name__ == "__main__":
    main()
