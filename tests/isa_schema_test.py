from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_isa import check_schema, load_schema
from gen_isa import render_all, write_outputs


class ScalarSchemaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.schema = load_schema()

    def test_current(self) -> None:
        self.assertEqual(check_schema(self.schema), [])

    def test_missing_or_malformed(self) -> None:
        for field, value in {
            "alignment_bytes": 2, "holon_bytes": 4, "holon_prefixes": [0, 1, 3],
            "scalar_profile": "rv32imc", "register_count": 16,
            "execution_environment": "host",
            "scalar_memory": {"address_bits": 64, "byte_order": "little", "misaligned": "emulate"},
            "scalar_traps": {"illegal_instruction": 0},
            "elf_profile": {"base": "rv32i2p1", "extensions": ["c2p0"], "stack_alignment": 4},
        }.items():
            with self.subTest(field=field):
                bad = copy.deepcopy(self.schema)
                bad["scalar"][field] = value
                self.assertTrue(check_schema(bad))
        del self.schema["scalar"]
        self.assertTrue(check_schema(self.schema))

    def test_patterns(self) -> None:
        for change in ("duplicate", "overlap", "missing", "format", "width", "prefix", "outside_mask"):
            with self.subTest(change=change):
                bad = copy.deepcopy(self.schema)
                entries = bad["scalar"]["instructions"]
                if change == "duplicate": entries.append(copy.deepcopy(entries[0]))
                elif change == "overlap": entries[1].update(value=entries[0]["value"], mask=entries[0]["mask"])
                elif change == "missing": entries.pop()
                elif change == "format": entries[0]["format"] = "unknown"
                elif change == "width": entries[0]["mask"] = "0x100000000"
                elif change == "prefix": entries[0]["value"] = "0x36"
                elif change == "outside_mask": entries[0]["value"] = "0xF0000037"
                self.assertTrue(check_schema(bad))

    def test_npu_operand_contract(self) -> None:
        for change in ("missing", "opcode", "zero_opcode", "prefix", "field_overlap", "field_end", "domain",
                       "role", "format", "hook", "type", "type_without_field", "registers"):
            with self.subTest(change=change):
                bad = copy.deepcopy(self.schema)
                npu = bad["npu"]
                match change:
                    case "missing": del bad["npu"]
                    case "opcode": npu["instructions"][1]["opcode"] = npu["instructions"][0]["opcode"]
                    case "zero_opcode": npu["instructions"][0]["opcode"] = 0
                    case "prefix": npu["families"]["vector"] = 3
                    case "field_overlap": npu["formats"]["binary"][1][1] = 12
                    case "field_end": npu["formats"]["binary"][1][1] = 63
                    case "domain": npu["formats"]["binary"][1][2] = 4
                    case "role": npu["roles"]["va"] = "untyped"
                    case "format": npu["instructions"][1]["format"] = "missing"
                    case "hook": npu["instructions"][1]["semantics"] = ""
                    case "type": npu["instructions"][1]["types"] = ["bf16"]
                    case "type_without_field": npu["instructions"][-1]["types"] = ["i32"]
                    case "registers": npu["register_counts"]["predicate"] = 1
                self.assertTrue(check_schema(bad))

    def test_machine_csrs(self) -> None:
        for change in ("missing", "duplicate", "overlap", "width", "readonly", "range"):
            with self.subTest(change=change):
                bad = copy.deepcopy(self.schema)
                frontend = bad["scalar"]
                csrs = frontend["machine_csrs"]
                if change == "missing": csrs.pop()
                elif change == "duplicate": csrs.append(copy.deepcopy(csrs[0]))
                elif change == "overlap": csrs[0]["address"] = "0xb03"
                elif change == "width": csrs[0]["reset"] = "0x100000000"
                elif change == "readonly": csrs[-1]["write_mask"] = "0x1"
                elif change == "range": frontend["machine_zero_csr_ranges"] = []
                self.assertTrue(check_schema(bad))

    def test_one_contract(self) -> None:
        self.assertEqual(set(render_all(self.schema)), {
            "docs/ISA_REFERENCE.md", "docs/NPU_OPERAND_REFERENCE.md",
            "sim/semantic/holon_npu_scalar_metadata.hpp",
            "sim/semantic/holon_npu_operand_metadata.hpp",
        })
        for key in ("instructions", "instruction_classes", "field_layout", "semantic_frontend", "semantic_npu"):
            with self.subTest(key=key):
                bad = copy.deepcopy(self.schema)
                bad[key] = []
                self.assertTrue(check_schema(bad))
        bad = copy.deepcopy(self.schema)
        bad["schema_version"] = 1
        self.assertTrue(check_schema(bad))

    def test_regeneration_preserves_unchanged_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outputs = render_all(self.schema)
            write_outputs(outputs, root)
            mtimes = {name: (root / name).stat().st_mtime_ns for name in outputs}
            self.schema["scalar"]["instructions"][0]["name"] = "SCALAR_EXAMPLE"
            updated = render_all(self.schema)
            write_outputs(updated, root)
            for name, content in updated.items():
                self.assertEqual((root / name).read_text(), content)
                if content == outputs[name]:
                    self.assertEqual((root / name).stat().st_mtime_ns, mtimes[name])


if __name__ == "__main__":
    unittest.main()
