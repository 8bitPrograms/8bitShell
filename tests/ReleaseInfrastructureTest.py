import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release", ROOT / "scripts/ci/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseInfrastructureTest(unittest.TestCase):
    def test_version_order(self):
        versions = ["0.1.0-alpha.1", "0.1.0-alpha.2", "0.1.0-beta.1", "0.1.0-rc.1", "0.1.0", "0.1.1-alpha.1"]
        numeric = [tuple(map(int, release.version_info(v)["msi"].split("."))) for v in versions]
        self.assertEqual(numeric, sorted(set(numeric)))
        self.assertEqual(release.version_info(versions[0])["unix"], "0.1.0~alpha.1")

    def test_invalid_versions_rejected(self):
        for version in ["v1.0.0", "1.0", "01.0.0", "1.0.0+local", "1.0.0-rc.0", "1.0.0-rc.100", "256.0.0", "0.0.65", "1.0.0;echo unsafe"]:
            with self.subTest(version=version), self.assertRaises(ValueError):
                release.version_info(version)

    def test_version_file_does_not_shadow_the_cpp_standard_header_on_windows(self):
        self.assertFalse((ROOT / "VERSION").exists())
        self.assertTrue((ROOT / "PROJECT_VERSION").is_file())

    @unittest.skipUnless(shutil.which("cmake"), "cmake is not installed")
    def test_cmake_uses_same_mapping(self):
        info = release.version_info((ROOT / "PROJECT_VERSION").read_text().strip())
        script = ROOT / "tests/VersionMapping.cmake"
        result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True, check=True)
        self.assertIn(f"RESULT={info['version']}|{info['msi']}|{info['unix']}", result.stderr)

    def test_publication_fails_closed(self):
        root = ROOT / "tests/fixtures/release-policy-incomplete"
        self.assertGreaterEqual(len(release.publication_errors(root)), 5)

    def test_sdk_lock_has_required_modules_and_hashes(self):
        lock = json.loads((ROOT / "packaging/qt-sdk.lock.json").read_text())
        common = {"qtbase", "qtdeclarative", "qtsvg", "qttools", "qttranslations", "qtshadertools"}
        for platform in ("linux-x64", "windows-x64"):
            entries = lock[platform]
            modules = {entry['module'] for entry in entries}
            self.assertTrue(common <= modules)
            self.assertEqual(len(entries), len(modules))
            for entry in entries:
                self.assertRegex(entry['sha256'], r"^[0-9a-f]{64}$")
                self.assertTrue(entry['url'].startswith("https://download.qt.io/online/qtsdkrepository/"))

    def test_puttygen_lock_and_windows_bundle_gate(self):
        lock = json.loads((ROOT / "packaging/puttygen.lock.json").read_text())
        self.assertEqual(set(lock), {"version", "source"})
        self.assertRegex(lock["version"], r"^\d+\.\d+$")
        source = lock["source"]
        self.assertEqual(
            source["url"],
            f"https://the.earth.li/~sgtatham/putty/{lock['version']}/putty-{lock['version']}.tar.gz",
        )
        self.assertRegex(source["sha256"], r"^[0-9a-f]{64}$")
        adapter = (ROOT / "scripts/ci/puttygen-windows-random.c").read_text()
        self.assertIn("BCryptGenRandom", adapter)
        cli_target = (ROOT / "scripts/ci/puttygen-windows-cli.cmake").read_text()
        self.assertIn("bitshell-puttygen-cli", cli_target)
        self.assertIn("cmdgen.c", cli_target)
        workflow = (ROOT / ".github/workflows/build.yml").read_text()
        self.assertIn("build-puttygen.py", (ROOT / "scripts/ci/build.py").read_text())
        self.assertNotIn("w64/puttygen.exe", workflow)
        packaging = (ROOT / "scripts/package-windows.ps1").read_text()
        self.assertIn("--verify", packaging)
        self.assertIn("Get-FileHash", packaging)
        self.assertTrue((ROOT / "third_party/PUTTY-LICENSE").is_file())

    def test_working_brand_and_package_surface_is_consistent(self):
        app_id = "io.github.8bitshell.App"
        self.assertTrue((ROOT / "assets/bitshell.svg").is_file())
        ET.parse(ROOT / "assets/bitshell.svg")
        desktop = (ROOT / f"packaging/{app_id}.desktop").read_text()
        self.assertIn("Name=8bitShell", desktop)
        self.assertIn("Exec=8bitShell", desktop)
        metadata = ET.parse(ROOT / f"packaging/{app_id}.metainfo.xml").getroot()
        self.assertEqual(metadata.findtext("id"), app_id)
        self.assertEqual(metadata.findtext("launchable"), f"{app_id}.desktop")
        self.assertEqual(metadata.findtext("provides/binary"), "8bitShell")
        packaging = (ROOT / "cmake/Packaging.cmake").read_text()
        for suffix in ("windows-x64", "linux-"):
            self.assertIn(suffix, packaging)
        workflow = (ROOT / ".github/workflows/build.yml").read_text()
        for extension in ("*.zip", "*.msi", "*.exe", "*.tar.gz", "*.deb", "*.rpm"):
            self.assertIn(extension, workflow)

    def test_windows_dpi_and_executable_icon_resources(self):
        manifest = ET.parse(ROOT / "packaging/windows.manifest").getroot()
        dpi_aware = manifest.find(".//{http://schemas.microsoft.com/SMI/2005/WindowsSettings}dpiAware")
        dpi_awareness = manifest.find(".//{http://schemas.microsoft.com/SMI/2016/WindowsSettings}dpiAwareness")
        self.assertEqual(dpi_aware.text, "true/pm")
        self.assertEqual(dpi_awareness.text, "PerMonitorV2,PerMonitor")

        icon_path = ROOT / "assets/bitshell.ico"
        data = icon_path.read_bytes()
        reserved, kind, count = struct.unpack_from("<HHH", data)
        self.assertEqual((reserved, kind), (0, 1))
        self.assertEqual(count, 10)
        sizes = set()
        for index in range(count):
            width, height, colors, reserved_byte, planes, bits, length, offset = struct.unpack_from(
                "<BBBBHHII", data, 6 + index * 16
            )
            self.assertEqual((colors, reserved_byte, planes, bits), (0, 0, 1, 32))
            self.assertLessEqual(offset + length, len(data))
            self.assertEqual(data[offset:offset + 8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(width, height)
            sizes.add(width or 256)
        self.assertEqual(sizes, {16, 20, 24, 32, 40, 48, 64, 96, 128, 256})

        resource = (ROOT / "packaging/windows-version.rc.in").read_text()
        self.assertIn("IDI_BIT8SHELL ICON", resource)
        self.assertIn("assets/bitshell.ico", resource)
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('QT_WINDOWS_APP_PROJECT_IDENTIFIER "io.github.8bitshell.App"', cmake)
        theme = (ROOT / "qml/Theme.qml").read_text()
        self.assertIn('Qt.platform.os === "windows" ? 1.2 : 1.0', theme)

    def test_ci_actions_pinned(self):
        for path in (ROOT / ".github/workflows").glob("*.yml"):
            for line in path.read_text().splitlines():
                match = re.search(r"uses:\s+(\S+)", line)
                if match and not match[1].startswith("./"):
                    self.assertRegex(match[1], r"@[0-9a-f]{40}$")

    def test_manual_ci_covers_native_targets_and_keeps_publication_separate(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text()
        self.assertIn("description: Build target", workflow)
        self.assertIn("default: all", workflow)
        self.assertIn("description: Build downloadable packages", workflow)
        self.assertIn("default: true", workflow)
        self.assertIn("description: Prepare a draft for a matching version tag", workflow)
        self.assertIn("--draft --verify-tag", workflow)
        self.assertIn("scripts/ci/release-check.py", workflow)
        matrix = (ROOT / "scripts/ci/matrix.py").read_text()
        for runner in ("windows-2022", "windows-11-arm", "ubuntu-22.04", "ubuntu-22.04-arm", "macos-15-intel", "macos-15"):
            self.assertIn(runner, matrix)
        self.assertIn("ENABLE_LEGACY32", workflow)
        self.assertIn("publish_draft", workflow)


if __name__ == '__main__':
    unittest.main()
