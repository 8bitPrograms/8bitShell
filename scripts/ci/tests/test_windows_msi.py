import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]

@unittest.skipUnless(os.name == "nt", "Windows PowerShell fixture")
class WindowsMsiUpdateTests(unittest.TestCase):
    def test_registered_product_without_an_uninstall_registry_key_uses_small_update(self):
        # Simulate the actual failure: native MSI says installed (5), while the
        # uninstall registry entry is unavailable. Never launch MSI in a test.
        self.check_plan(5, "Reinstall", True)

    def test_first_install_and_advertised_product_do_not_reinstall_missing_features(self):
        # REINSTALL=ALL on a fresh product installs no features.
        for state in (-1, 1, 2):
            with self.subTest(state=state):
                self.check_plan(state, "Install", False)

    def test_service_side_1638_gets_one_in_place_retry_but_other_errors_do_not(self):
        cases = [
            (-1, [1638, 0], 2, True, 0),
            (-1, [1638, 1638], 2, True, 1638),
            (-1, [1601], 1, False, 1601),
            (5, [1638], 1, False, 1638),
        ]
        for state, exits, call_count, retried, final_exit in cases:
            with self.subTest(state=state, exits=exits), tempfile.TemporaryDirectory() as directory:
                helper = str(ROOT / "scripts/ci/windows-msi.ps1").replace("'", "''")
                script = Path(directory) / "retry.ps1"
                script.write_text(
                    "$ErrorActionPreference='Stop'\n"
                    f". '{helper}'\n"
                    "$calls=New-Object Collections.ArrayList\n"
                    f"$exitCodes=@({','.join(map(str, exits))})\n"
                    "$runner={param($ArgumentList)\n"
                    " [void]$calls.Add([string[]]$ArgumentList)\n"
                    " return $exitCodes[$calls.Count-1]\n"
                    "}.GetNewClosure()\n"
                    f"$query={{param($Code) {state}}}\n"
                    "$plan=Get-Bit8MsiInstallPlan -Package 'C:\\builds\\Pad.msi' "
                    "-ProductCode '{C052F9E5-B5EB-8AAA-A22B-B03DE7553F5B}' "
                    "-Log 'C:\\logs\\Pad.log' -QueryProductState $query\n"
                    "$outcome=Invoke-Bit8MsiInstallPlan -Plan $plan -RunProcess $runner\n"
                    "[pscustomobject]@{Outcome=$outcome;Calls=$calls.ToArray()} | ConvertTo-Json -Depth 8 -Compress\n",
                    encoding="utf-8",
                )
                result = subprocess.run(
                    ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                    text=True, capture_output=True, timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                data = json.loads(result.stdout.strip().splitlines()[-1])
                self.assertEqual(len(data["Calls"]), call_count)
                self.assertEqual(data["Outcome"]["Retried"], retried)
                self.assertEqual(data["Outcome"]["ExitCode"], final_exit)
                for command in data["Calls"]:
                    self.assertNotIn("/x", command)
                    self.assertEqual(command[1], '"C:\\builds\\Pad.msi"')
                if retried:
                    self.assertNotIn("REINSTALL=ALL", data["Calls"][0])
                    self.assertIn("REINSTALL=ALL", data["Calls"][1])
                    self.assertIn("REINSTALLMODE=vamus", data["Calls"][1])
                    self.assertEqual(data["Outcome"]["LogPath"], "C:\\logs\\Pad.reinstall.log")

    def test_preflight_waits_for_installed_app_but_ignores_a_separate_portable_copy(self):
        for portable, expected_polls, expected_pauses in [(False, 2, 1), (True, 1, 0)]:
            with self.subTest(portable=portable), tempfile.TemporaryDirectory() as directory:
                helper = str(ROOT / "scripts/ci/windows-msi.ps1").replace("'", "''")
                script = Path(directory) / "close.ps1"
                process_path = "C:\\portable\\8bitPad.exe" if portable else "C:\\apps\\8bitPad\\bin\\8bitPad.exe"
                script.write_text(
                    "$ErrorActionPreference='Stop'\n"
                    f". '{helper}'\n"
                    "function Stop-Process {throw 'Must not force-close user work'}\n"
                    "$counts=[pscustomobject]@{Polls=0;Pauses=0}\n"
                    "$find={param($Name)\n"
                    " $counts.Polls++\n"
                    f" if($counts.Polls -eq 1){{[pscustomobject]@{{Id=28884;Path='{process_path}'}}}}\n"
                    "}.GetNewClosure()\n"
                    "$pause={$counts.Pauses++}.GetNewClosure()\n"
                    "Wait-Bit8MsiApplicationExit -Application 8bitPad -Executable 'C:\\apps\\8bitPad\\bin\\8bitPad.exe' "
                    "-FindProcesses $find -Pause $pause\n"
                    "$counts | ConvertTo-Json -Compress\n",
                    encoding="utf-8",
                )
                result = subprocess.run(
                    ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                    text=True, capture_output=True, timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                counts = json.loads(result.stdout.strip().splitlines()[-1])
                self.assertEqual(counts, {"Polls": expected_polls, "Pauses": expected_pauses})

    def test_preflight_timeout_stops_before_msi_and_does_not_kill_the_app(self):
        with tempfile.TemporaryDirectory() as directory:
            helper = str(ROOT / "scripts/ci/windows-msi.ps1").replace("'", "''")
            script = Path(directory) / "timeout.ps1"
            script.write_text(
                "$ErrorActionPreference='Stop'\n"
                f". '{helper}'\n"
                "function Stop-Process {throw 'Must not force-close user work'}\n"
                "$find={param($Name)[pscustomobject]@{Id=28884;Path='C:\\apps\\8bitPad\\bin\\8bitPad.exe'}}\n"
                "try {\n"
                " Wait-Bit8MsiApplicationExit -Application 8bitPad -Executable 'C:\\apps\\8bitPad\\bin\\8bitPad.exe' "
                "-FindProcesses $find -TimeoutSeconds 0\n"
                " throw 'Expected a running-app error'\n"
                "} catch {\n"
                " if($_.Exception.Message -notlike '*still running*28884*'){throw}\n"
                " Write-Output 'TIMEOUT_WITHOUT_KILL'\n"
                "}\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                text=True, capture_output=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("TIMEOUT_WITHOUT_KILL", result.stdout)

    def check_plan(self, state, action, reinstall):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "plan.ps1"
            helper = str(ROOT / "scripts/ci/windows-msi.ps1").replace("'", "''")
            script.write_text(
                "$ErrorActionPreference='Stop'\n"
                f". '{helper}'\n"
                # A former registry lookup would fail this fixture immediately.
                "function Test-Path { throw 'Registry probing is not an MSI product-state query' }\n"
                "$package='C:\\fixture path\\pad; literal & text.msi'\n"
                "$log='C:\\fixture path\\update.log'\n"
                f"$query={{param($Code) {state}}}\n"
                "$plan=Get-Bit8MsiInstallPlan -Package $package "
                "-ProductCode '{C052F9E5-B5EB-8AAA-A22B-B03DE7553F5B}' "
                "-Log $log -QueryProductState $query -Quiet\n"
                "$plan | ConvertTo-Json -Compress -Depth 4\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                text=True, capture_output=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual(plan["Action"], action)
            self.assertEqual(plan["ProductState"], state)
            arguments = plan["Arguments"]
            self.assertEqual(arguments[:2], ["/i", '"C:\\fixture path\\pad; literal & text.msi"'])
            self.assertIn("/qn", arguments)
            self.assertIn("/norestart", arguments)
            self.assertNotIn("/x", arguments)
            if reinstall:
                self.assertIn("REINSTALL=ALL", arguments)
                self.assertIn("REINSTALLMODE=vamus", arguments)
            else:
                self.assertFalse(any(arg.startswith("REINSTALL") for arg in arguments))

if __name__ == "__main__":
    unittest.main()
