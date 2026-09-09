"""Mutation checks for autonomous simulator build provenance."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class BuildAudit(unittest.TestCase):
    def setUp(self):
        self.entries = [
            {"file": name, "arguments": ["c++", "-std=c++26", "-frounding-math", "-ffp-contract=off"]}
            for name in ("holon_npu.cc", "holon_npu_npu.cpp", "holon_npu_semantic.cpp")
        ]

    def check_entries(self, entries, succeeds, diagnostic=""):
        with tempfile.TemporaryDirectory() as directory:
            commands = Path(directory) / "commands.json"
            commands.write_text(json.dumps(entries))
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/check_gem5_cxx_standard.py"), str(commands)],
                text=True, capture_output=True, check=False,
            )
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
        self.assertIn(diagnostic, result.stdout + result.stderr)

    def test_valid_and_missing_sources(self):
        self.check_entries(self.entries, True)
        self.check_entries(self.entries[:-1], False, "autonomous model source is absent")
        self.check_entries([], False, "no C++ translation units")

    def test_effective_standard(self):
        for standard in ("c++17", "c++20", "c++23"):
            with self.subTest(standard=standard):
                entries = copy.deepcopy(self.entries)
                entries[0]["arguments"].append(f"-std={standard}")
                self.check_entries(entries, False, "effective standard")

    def test_floating_point_overrides(self):
        for flag in ("-ffp-contract=fast", "-ffast-math", "-Ofast", "-fno-rounding-math", "-fno-signed-zeros"):
            with self.subTest(flag=flag):
                entries = copy.deepcopy(self.entries)
                entries[1]["arguments"].append(flag)
                self.check_entries(entries, False, "floating-point flags")
        entries = copy.deepcopy(self.entries)
        entries[1]["arguments"].remove("-frounding-math")
        self.check_entries(entries, False, "floating-point flags")

    def test_removed_adapter(self):
        self.check_entries(self.entries + [{"file": "holon_npu_device.cc", "arguments": ["c++", "-std=c++26"]}],
                           False, "superseded Host device")

    def test_test_side_sources_are_not_linked(self):
        for source in ("holon_npu_execution.cpp", "holon_npu_runtime.cpp"):
            with self.subTest(source=source):
                self.check_entries(self.entries + [{"file": source, "arguments": ["c++", "-std=c++26"]}],
                                   False, "test-side runner/builder")

    def test_compiler_alias_preserves_binary_identity(self):
        spec = importlib.util.spec_from_file_location("build_gem5", ROOT / "tools/build_gem5.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            compiler, alias, custom = root / "target-g++-16", root / "g++-16", root / "c++"
            compiler.write_text("compiler identity fixture")
            alias.symlink_to(compiler)
            custom.write_text("different compiler identity")
            with patch.object(module.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stdout="16.2.0")):
                self.assertEqual(module.gem5_recognized_compiler(str(compiler)), str(alias))
                self.assertEqual(module.gem5_recognized_compiler(str(alias)), str(alias))
                self.assertEqual(module.gem5_recognized_compiler(str(custom)), str(custom))


if __name__ == "__main__":
    unittest.main()
