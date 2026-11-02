import hashlib
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "local-windows-sources.py"
sys.path.insert(0, str(SCRIPT.parent))
spec = importlib.util.spec_from_file_location("local_sources", SCRIPT)
sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sources)


class LocalSourceTests(unittest.TestCase):
    def test_missing_or_tampered_sources_cannot_be_bundled(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            with self.assertRaisesRegex(ValueError, "Missing corresponding"):
                sources.qt_archives(cache, "6.11.2")
            for module in sources.MODULES:
                archive = cache / f"{module}-everywhere-src-6.11.2.tar.xz"
                archive.write_bytes(module.encode())
                archive.with_name(archive.name + ".sha256").write_text(
                    hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name)
            self.assertEqual(len(sources.qt_archives(cache, "6.11.2")), len(sources.MODULES) * 2)
            archive.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                sources.qt_archives(cache, "6.11.2")
