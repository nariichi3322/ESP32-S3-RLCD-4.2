import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from companion.install_claude import SCRIPT_NAME, install

SCRIPT = Path(__file__).resolve().parents[1] / SCRIPT_NAME


def run(local, mode, stdin, *extra):
    env = dict(os.environ, LOCALAPPDATA=str(local))
    return subprocess.run([sys.executable, str(SCRIPT), mode, *extra], input=stdin,
                          capture_output=True, env=env, timeout=30)


class ScriptTests(unittest.TestCase):
    def test_statusline_stores_limits_and_chains(self):
        with tempfile.TemporaryDirectory() as tmp:
            payload = json.dumps({"session_id": "s", "rate_limits": {
                "five_hour": {"used_percentage": 10, "resets_at": 5}, "junk": 1}}).encode()
            chain = f'"{sys.executable}" -c "import sys;print(sys.stdin.read().count(\'five_hour\'))"'
            result = run(tmp, "--statusline", payload, "--chain", chain)
            self.assertEqual((result.returncode, result.stdout.strip()), (0, b"1"))
            stored = json.loads((Path(tmp) / "CodexUsageDisplay" / "claude-rate-limits.json").read_text())
            self.assertEqual(stored["five_hour"]["resets_at"], 5)
            self.assertNotIn("junk", stored)
            prev = Path(tmp) / "prev.json"
            prev.write_text(json.dumps({"statusLine": {"type": "command", "command": chain}}))
            self.assertEqual(run(tmp, "--statusline", payload, "--chain-file", str(prev)).stdout.strip(), b"1")

    def test_bad_input_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            for mode in ("--statusline", "--hook"):
                for data in (b"", b"not json", b"[1]", b'{"rate_limits":5}'):
                    result = run(tmp, mode, data)
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(result.stdout, b"")

    def test_hook_tracks_sessions_without_prompt_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CodexUsageDisplay" / "claude-sessions.json"

            def event(name, sid):
                run(tmp, "--hook", json.dumps({"hook_event_name": name, "session_id": sid,
                                               "prompt": "SECRET"}).encode())
                return json.loads(path.read_text())["sessions"]

            self.assertEqual(set(event("UserPromptSubmit", "a")), {"a"})
            self.assertEqual(set(event("UserPromptSubmit", "b")), {"a", "b"})
            self.assertEqual(set(event("Stop", "a")), {"b"})
            self.assertEqual(event("SessionEnd", "b"), {})
            self.assertNotIn("SECRET", path.read_text())


class InstallerTests(unittest.TestCase):
    def test_install_idempotent_preserves_and_uninstall_restores(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            other = {"hooks": [{"type": "command", "command": "other"}]}
            original = {"theme": "dark", "hooks": {"Stop": [other]},
                        "enabledPlugins": {"tokens@local-mods": True},
                        "statusLine": {"type": "command", "command": "my-line --x", "padding": 1}}
            (root / "settings.json").write_text(json.dumps(original))
            py, script = Path("C:/Program Files/Python/python.exe"), Path("C:/a b/" + SCRIPT_NAME)
            install(root, script, py, False)
            first = json.loads((root / "settings.json").read_text())
            install(root, script, py, False)
            self.assertEqual(first, json.loads((root / "settings.json").read_text()))
            self.assertEqual(first["theme"], "dark")
            self.assertIn(other, first["hooks"]["Stop"])
            self.assertEqual(len(first["hooks"]["Stop"]), 2)
            self.assertEqual(set(first["hooks"]), {"UserPromptSubmit", "Stop", "SessionEnd"})
            self.assertIn("--chain-file", first["statusLine"]["command"])
            self.assertEqual(first["enabledPlugins"],
                             {"tokens@local-mods": True, "rlcd-usage-bridge@rlcd-local": True})
            self.assertEqual(first["extraKnownMarketplaces"]["rlcd-local"]["source"]["path"],
                             str(script.parent / "rlcd-usage-bridge"))
            self.assertTrue(list(root.glob("settings.json.bak-*")))
            install(root, script, py, True)
            self.assertEqual(json.loads((root / "settings.json").read_text()), original)
            self.assertFalse((root / "claude-display-previous-statusline.json").exists())

    def test_install_without_previous_statusline_then_uninstall(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            install(root, Path(SCRIPT_NAME), Path("python.exe"), False)
            settings = json.loads((root / "settings.json").read_text())
            self.assertNotIn("--chain", settings["statusLine"]["command"])
            install(root, Path(SCRIPT_NAME), Path("python.exe"), True)
            self.assertEqual(json.loads((root / "settings.json").read_text()), {})


if __name__ == "__main__":
    unittest.main()
