from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_coverage import baseline_failures, validate_run
from check_macro_policy import check as check_macros
from check_repository import anchors, check, compilation_units, document_errors


class CoverageEvidenceTests(unittest.TestCase):
    def test_missing_extra_stale_or_changed_data_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            data = build / "CMakeFiles/test.dir/test.cpp.gcda"
            data.parent.mkdir(parents=True)
            run = {"digest": "current", "units": [str(data)], "start_ns": 1_000_000_000}
            with self.assertRaisesRegex(ValueError, "mismatch"):
                validate_run(build, run, {data}, "current")
            data.touch()
            validate_run(build, run, {data}, "current")
            with self.assertRaisesRegex(ValueError, "changed"):
                validate_run(build, run, {data}, "changed")
            extra = data.with_name("obsolete.gcda")
            extra.touch()
            with self.assertRaisesRegex(ValueError, "extra"):
                validate_run(build, run, {data}, "current")
            extra.unlink()
            os.utime(data, ns=(1, 1))
            with self.assertRaisesRegex(ValueError, "stale"):
                validate_run(build, run, {data}, "current")

    def test_thresholds_are_real_gates(self):
        baseline = {"minimum_percent": {"lines": 80, "functions": 85}}
        summary = {"metrics": {"lines": {"percent": 79.99}, "functions": {"percent": 90}}}
        self.assertEqual(baseline_failures(summary, baseline), ["lines coverage below 80%"])
        summary["metrics"]["lines"]["percent"] = 80
        self.assertFalse(baseline_failures(summary, baseline))
        with self.assertRaisesRegex(ValueError, "nonzero"):
            baseline_failures(summary, {"minimum_percent": {"lines": 0, "functions": 0}})


class RepositoryTests(unittest.TestCase):
    def test_macro_policy_and_ignored_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("project(HolonNPU)\n")
            (root / "include").mkdir()
            header = root / "include/api.hpp"
            header.write_text('#pragma once\n#ifdef __cplusplus\nextern "C" {\n#endif\n')
            (root / "build").mkdir()
            (root / "build/CMakeLists.txt").write_text("target_compile_definitions(ignored PRIVATE EXTERNAL)\n")
            self.assertFalse(check_macros(root))
            header.write_text("#define PROJECT_BEHAVIOR 1\n")
            self.assertTrue(check_macros(root))
            header.write_text("#if defined(PROJECT_BEHAVIOR)\n#endif\n")
            self.assertTrue(check_macros(root))
            header.write_text("")
            (root / "CMakeLists.txt").write_text("target_compile_definitions(core PRIVATE PROJECT_BEHAVIOR)\n")
            self.assertTrue(check_macros(root))

    def test_relative_links_and_anchors(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "README.md").write_text("[valid](docs/ISA.md#operand-state)\n")
            (root / "docs").mkdir()
            (root / "docs/ISA.md").write_text("# ISA\n## Operand State\n")
            self.assertFalse(document_errors(root))
            (root / "docs/ISA.md").write_text("# ISA\n")
            self.assertTrue(document_errors(root))
            (root / "README.md").write_text("[outside](../missing.md)\n")
            self.assertTrue(document_errors(root))
        self.assertEqual(anchors("## Same\n## Same\n"), {"same", "same-1"})

    def test_retired_backend_and_build_dependency_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("project(HolonNPU)\n")
            (root / "CMakePresets.json").write_text("{}\n")
            self.assertFalse(check(root))
            (root / "rtl").mkdir()
            self.assertIn("retired tree present: rtl", check(root))
            (root / "rtl").rmdir()
            (root / "CMakeLists.txt").write_text("find_package(verilator REQUIRED)\n")
            self.assertTrue(check(root))

    def test_compilation_database_scope_and_standard(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            (root / "CMakeLists.txt").write_text("")
            (root / "CMakePresets.json").write_text("{}")
            (root / "sim").mkdir()
            source = root / "sim/core.cpp"
            source.write_text("")
            entry = {"directory": str(build), "file": str(source),
                     "arguments": ["c++", "-std=c++26", "-o", "CMakeFiles/core.dir/core.cpp.o", "-c", str(source)]}
            database = build / "compile_commands.json"
            database.write_text(json.dumps([entry]))
            self.assertEqual(set(compilation_units(build)), {source})
            self.assertFalse(check(root, build))
            entry["arguments"].insert(2, "-std=c++17")
            database.write_text(json.dumps([entry]))
            self.assertTrue(check(root, build))
            entry["arguments"][entry["arguments"].index("-o") + 1] = "../outside.o"
            database.write_text(json.dumps([entry]))
            with self.assertRaisesRegex(ValueError, "outside"):
                compilation_units(build)


if __name__ == "__main__":
    unittest.main()
