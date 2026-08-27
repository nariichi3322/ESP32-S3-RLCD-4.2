import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from companion.codex_display.app_server import find_codex_binary


class AppServerTests(unittest.TestCase):
    def test_configured_binary_has_priority(self):
        with patch.dict(os.environ, {"CODEX_BIN": r"C:\tools\codex.exe"}):
            self.assertEqual(find_codex_binary(), r"C:\tools\codex.exe")

    def test_finds_codex_bundled_in_system_vscode_data_directory(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            binary = (Path(temporary_directory) / "Visual Studio Code" / "data" /
                      "extensions" / "openai.chatgpt-test" / "bin" /
                      "windows-x86_64" / "codex.exe")
            binary.parent.mkdir(parents=True)
            binary.touch()
            environment = {
                "ProgramFiles": temporary_directory,
                "USERPROFILE": str(Path(temporary_directory) / "user"),
            }
            with patch.dict(os.environ, environment, clear=True), \
                    patch("companion.codex_display.app_server.shutil.which",
                          return_value=None):
                self.assertEqual(find_codex_binary(), str(binary))
