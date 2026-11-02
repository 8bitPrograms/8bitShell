"""A release upload must stop before GitHub when its local bundle is incomplete."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(os.name == "nt", "Windows PowerShell release helper")
class PreviewPublishTests(unittest.TestCase):
    def test_missing_changed_or_stale_packages_stop_before_upload(self):
        with tempfile.TemporaryDirectory(prefix="publish-check-", dir=ROOT / "build") as directory:
            root = Path(directory)
            fixture = root / "repo"
            (fixture / "scripts").mkdir(parents=True)
            script = fixture / "scripts/publish-windows-preview.ps1"
            shutil.copy2(ROOT / "scripts/publish-windows-preview.ps1", script)
            (fixture / "PROJECT_VERSION").write_text("1.0.1\n")
            subprocess.run(["git", "init", "--quiet", str(fixture)], check=True)
            subprocess.run(["git", "-C", str(fixture), "add", "."], check=True)
            subprocess.run(["git", "-C", str(fixture), "-c", "user.name=Fixture",
                            "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
                            "commit", "--quiet", "-m", "Local fixture [skip ci]"], check=True)
            head = subprocess.check_output(["git", "-C", str(fixture), "rev-parse", "HEAD"], text=True).strip()
            environment = os.environ.copy()
            environment["PSMODULEPATH"] = str(Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/Modules")
            # This fixture repository has no remote. Even a regression cannot
            # push the real project's source or create a release from this test.
            for scenario, expected in (("missing", "Missing package/checksum"),
                                       ("changed", "Package changed"),
                                       ("stale", "exact source commit")):
                with self.subTest(scenario=scenario):
                    out = root / scenario
                    out.mkdir()
                    names = ["8bitShell-1.0.1-windows-x64" + suffix
                             for suffix in ("-setup.exe", ".msi", ".zip")]
                    hashes = []
                    for name in names:
                        payload = name.encode()
                        (out / name).write_bytes(payload)
                        hashes.append(hashlib.sha256(payload).hexdigest() + "  " + name)
                    (out / "SHA256SUMS.txt").write_text("\n".join(hashes) + "\n")
                    (out / "build-info.json").write_text(json.dumps({
                        "version": "1.0.1", "commit": "0" * 40 if scenario == "stale" else head}))
                    if scenario == "missing":
                        (out / names[1]).unlink()
                    elif scenario == "changed":
                        (out / names[1]).write_bytes(b"changed after verification")
                    result = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                             "-File", str(script), "-PackageDirectory", str(out)],
                                            capture_output=True, text=True, timeout=30, env=environment)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, result.stdout + result.stderr)
