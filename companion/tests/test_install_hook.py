import unittest
from pathlib import Path

from companion.install_hook import updated_config


class InstallHookTests(unittest.TestCase):
    def test_install_is_idempotent_and_preserves_other_hooks(self):
        other = {"hooks": [{"type": "command", "command": "other"}]}
        script = Path("codex_display_event.py")
        first = updated_config({"hooks": {"Stop": [other]}}, script, False,
                               Path("python.exe"))
        second = updated_config(first, script, False, Path("python.exe"))
        self.assertEqual(first, second)
        self.assertIn(other, second["hooks"]["Stop"])
        removed = updated_config(second, script, True, Path("python.exe"))
        self.assertEqual(removed["hooks"]["Stop"], [other])
