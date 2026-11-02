import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import build

class LocalSmokeTests(unittest.TestCase):
    def test_normal_windows_user_never_uses_the_account_helper(self):
        with patch.object(build.platform,"system",return_value="Windows"), \
             patch.object(build,"windows_elevated",return_value=False), \
             patch.object(build,"run",return_value="started") as direct, \
             patch.object(build,"_windows_unelevated",side_effect=AssertionError("account helper")):
            self.assertEqual(build.smoke_run(["fixture.exe"],{"PATH":"fixture"},10,Path("work"),Path("stage")),"started")
            direct.assert_called_once_with(["fixture.exe"],env={"PATH":"fixture"},timeout=10)

    def test_elevated_ci_keeps_the_alternate_user_path(self):
        with patch.object(build.platform,"system",return_value="Windows"), \
             patch.object(build,"windows_elevated",return_value=True), \
             patch.object(build,"_windows_unelevated",return_value=0) as alternate, \
             patch.object(build,"run",side_effect=AssertionError("elevated direct launch")):
            build.smoke_run(["fixture.exe"],{},10,Path("nonexistent-fixture-directory"),Path("stage"))
            alternate.assert_called_once()

if __name__ == "__main__":unittest.main()
