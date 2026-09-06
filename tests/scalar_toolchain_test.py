from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
from pathlib import Path


def run(*command: str | Path) -> str:
    result = subprocess.run([str(arg) for arg in command], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"command failed: {' '.join(map(str, command))}\n{result.stdout}{result.stderr}")
    return result.stdout


def sample(entry: dict) -> tuple[str, str]:
    name = entry["name"].lower()
    match entry["format"]:
        case "r": assembly = f"{name} x31, x1, x30"
        case "i":
            assembly = (f"{name} x31, -2048(x1)" if name in {"lb", "lh", "lw", "lbu", "lhu", "jalr"}
                        else f"{name} x31, x1, -2048")
        case "s": assembly = f"{name} x30, 2047(x1)"
        case "b": return f"{name} x1, x30, .-4096", f"{name} x1, x30, -4096"
        case "j": return f"{name} x31, .+1048574", f"{name} x31, 1048574"
        case "u": assembly = f"{name} x31, 0xabcde"
        case "shift": assembly = f"{name} x31, x1, 31"
        case "csr": assembly = f"{name} x31, 0x340, x1"
        case "csr_immediate": assembly = f"{name} x31, 0x340, 31"
        case "fence": return "fence iorw, iorw", "fence fm=0, pred=0xf, succ=0xf"
        case "system": assembly = name
        case other: raise ValueError(f"unsupported operand format {other}")
    return assembly, assembly


def main() -> None:
    parser = argparse.ArgumentParser(description="Cross-check Holon scalar decoding against upstream RISC-V tools.")
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler = Path(shutil.which(args.compiler) or args.compiler).absolute()
    cxx = compiler.with_name(compiler.name.replace("gcc", "g++", 1))
    objcopy = run(compiler, "-print-prog-name=objcopy").strip()
    readelf = run(compiler, "-print-prog-name=readelf").strip()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    entries = json.loads((root / "spec/holon_npu_isa.json").read_text())["semantic_frontend"]["instructions"]
    samples = [sample(entry) for entry in entries]
    assembly = output / "scalar.S"
    assembly.write_text(".option norvc\n.option norelax\n.text\n.balign 4\n.global decode_probe\ndecode_probe:\n"
                        + "\n".join(asm for asm, _ in samples) + "\n")
    flags = ["-march=rv32im_zicsr", "-mabi=ilp32", "-mno-relax"]
    elf, binary = output / "scalar.elf", output / "scalar.bin"
    run(compiler, *flags, "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-Wl,--build-id=none", "-Wl,--no-relax", "-Wl,-Ttext=0x1000", "-Wl,-e,decode_probe",
        assembly, "-o", elf)
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", elf.read_bytes())
    if header[0][:6] != b"\x7fELF\x01\x01" or header[2] != 243 or header[7] != 0:
        raise RuntimeError("expected ELF32 little-endian RISC-V with soft-float/no-RVC flags")
    (output / "elf.txt").write_text(run(readelf, "-h", "-A", elf))
    run(objcopy, "-O", "binary", "-j", ".text", elf, binary)
    actual = run(args.decoder.resolve(), "--scalar-words", binary).splitlines()
    expected = [text for _, text in samples]
    if actual != expected:
        raise RuntimeError(f"upstream instruction decode mismatch\nexpected={expected}\nactual={actual}")
    (output / "disassembly.txt").write_text("\n".join(actual) + "\n")

    # Compile real C/C++ rather than relying only on hand-written assembly.
    body = """
unsigned probe(const unsigned* p, unsigned count) {
    unsigned total = 0;
    for (unsigned i = 0; i < count; ++i) total += p[i] * count / (i + 1);
    return total;
}
"""
    sources = {
        "c": "static_assert(__STDC_VERSION__ >= 202311L);\nstatic_assert(sizeof(void*) == 4);\n" + body,
        "cpp": "static_assert(__cplusplus > 202302L);\nstatic_assert(sizeof(void*) == 4);\n" + body,
    }
    for suffix, source in sources.items():
        path, obj, raw = output / f"probe.{suffix}", output / f"{suffix}.o", output / f"{suffix}.bin"
        path.write_text(source)
        run(compiler if suffix == "c" else cxx, *flags, "-std=c23" if suffix == "c" else "-std=c++26",
            "-ffreestanding", "-fno-pic", "-fno-pie", "-O2", "-Wall", "-Wextra", "-Werror", "-c", path, "-o", obj)
        run(objcopy, "-O", "binary", "-j", ".text", obj, raw)
        decoded = run(args.decoder.resolve(), "--scalar-words", raw)
        (output / f"{suffix}-disassembly.txt").write_text(decoded)
    (output / "compiler.txt").write_text(run(compiler, "--version"))
    for language, tool, standard in (("c", compiler, "c23"), ("c++", cxx, "c++26")):
        directory = output / ("execution-c" if language == "c" else "execution-cpp")
        directory.mkdir(exist_ok=True)
        obj = directory / "probe.o"
        run(tool, *flags, "-x", language, f"-std={standard}", "-ffreestanding", "-fno-pic",
            "-fno-pie", "-fno-common", "-fno-inline", "-fno-omit-frame-pointer", "-fdata-sections",
            "-O2", "-Wall", "-Wextra", "-Werror", "-c", root / "tests/rv32_memory_probe.c", "-o", obj)
        program = directory / "probe.elf"
        run(compiler, *flags, "-nostdlib", "-nostartfiles", "-static", "-no-pie",
            "-Wl,--build-id=none", "-Wl,--no-relax", f"-Wl,-T,{root / 'tests/rv32_memory.ld'}",
            "-std=c23", "-ffreestanding", "-fno-builtin", "-O2",
            root / "tests/rv32_memory_start.S", root / "sim/guest/freestanding.c", obj, "-o", program)
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", program.read_bytes())
        if header[0][:6] != b"\x7fELF\x01\x01" or header[2] != 243 or header[7] != 0 or header[4] != 0x1000:
            raise RuntimeError("execution probe requires ELF32 little-endian RV32/no-RVC at expected entry")
        (directory / "elf.txt").write_text(run(readelf, "-h", "-A", "-l", program))
        code, data = directory / "code.bin", directory / "data.bin"
        run(objcopy, "-O", "binary", "-j", ".text", program, code)
        run(objcopy, "-O", "binary", "-j", ".data", program, data)
        result = run(args.runner.resolve(), code, data)
        (directory / "execution.txt").write_text(result)
        print(f"{standard}: {result.strip()}")
    print(f"upstream scalar oracle: {len(expected)}/{len(expected)} instructions, C23/C++26 decode PASS")
    print("Compiled scalar probes exercise shared hart + physical routing; full Holon machine/ELF loading remains separate.")


if __name__ == "__main__":
    main()
