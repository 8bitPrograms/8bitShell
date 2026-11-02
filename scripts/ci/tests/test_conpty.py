import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("prepare_conpty", ROOT / "scripts/ci/prepare-conpty.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

class ConptyPreparationTests(unittest.TestCase):
    def test_offline_inventory_is_checked_before_any_file_is_copied(self):
        # Tiny synthetic files exercise the hash contract; no DLL is loaded.
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            root = Path(directory)
            (root / "packaging").mkdir()
            source = root / "source"
            source.mkdir()
            inventory = {"conpty.dll": b"fixture-dll", "OpenConsole.exe": b"fixture-host"}
            lock = {"architectures": {"x64": {name: hashlib.sha256(data).hexdigest()
                                               for name, data in inventory.items()}}}
            (root / "packaging/conpty.lock.json").write_text(json.dumps(lock))
            (root / "packaging/CONPTY-LICENSE.txt").write_text("MIT fixture")
            for name, data in inventory.items():
                (source / name).write_bytes(data)
            output = root / "output"
            with patch.object(MODULE, "ROOT", root):
                MODULE.prepare(output, source)
                for name, data in inventory.items():
                    self.assertEqual((output / name).read_bytes(), data)
                self.assertEqual((output / "LICENSE.txt").read_text(), "MIT fixture")
                # A verified cache must not download anything on the next run.
                with patch.object(MODULE.urllib.request, "urlopen", side_effect=AssertionError("network")):
                    MODULE.prepare(output)
                (source / "OpenConsole.exe").write_bytes(b"modified")
                fresh = root / "fresh-output"
                with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                    MODULE.prepare(fresh, source)
                self.assertFalse(fresh.exists())

if __name__ == "__main__":
    unittest.main()
