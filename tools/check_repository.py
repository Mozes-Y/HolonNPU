#!/usr/bin/env python3
"""Check active source ownership, build dependencies and local document links."""
from __future__ import annotations

import argparse
import json
import re
import shlex
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def compilation_units(build: Path) -> dict[Path, tuple[Path, list[str]]]:
    units = {}
    for entry in json.loads((build / "compile_commands.json").read_text()):
        directory = Path(entry["directory"])
        source = (directory / entry["file"]).resolve()
        args = entry.get("arguments") or shlex.split(entry["command"])
        if source.suffix != ".cpp":
            continue
        output = Path(args[args.index("-o") + 1])
        obj = (directory / output).resolve()
        if not obj.is_relative_to(build / "CMakeFiles"):
            raise ValueError(f"object outside owned build tree: {obj}")
        if source in units:
            raise ValueError(f"duplicate translation unit: {source}")
        units[source] = (obj, args)
    if not units:
        raise ValueError("empty C++ compilation database")
    return units


def anchors(text: str) -> set[str]:
    found, counts = set(), {}
    for heading in re.findall(r"^#{1,6}\s+(.+)$", text, re.MULTILINE):
        slug = re.sub(r"[^\w -]", "", heading.lower()).replace(" ", "-")
        count = counts.get(slug, 0)
        found.add(slug if not count else f"{slug}-{count}")
        counts[slug] = count + 1
    return found


def document_errors(root: Path) -> list[str]:
    errors = []
    documents = list(root.glob("*.md"))
    for directory in ("docs", "include", "sw", "tests"):
        documents.extend((root / directory).rglob("*.md"))
    for path in documents:
        text = path.read_text()
        # Repository docs use inline links; remote availability is not a local gate.
        for link in re.findall(r"\[[^\]\n]*\]\(([^)\n]+)\)", text):
            url = urlsplit(link.strip("<>"))
            if url.scheme or url.netloc:
                continue
            target = (path.parent / unquote(url.path)).resolve() if url.path else path
            if not target.is_relative_to(root) or not target.exists():
                errors.append(f"{path.relative_to(root)}: missing/outside link {link}")
            elif url.fragment and target.suffix == ".md" and unquote(url.fragment) not in anchors(target.read_text()):
                errors.append(f"{path.relative_to(root)}: missing anchor {link}")
    return errors


def check(root: Path, build: Path | None = None) -> list[str]:
    errors = document_errors(root)
    for directory in ("rtl", "sim/rtl", "sim/gem5", "legacy"):
        if (root / directory).exists():
            errors.append(f"retired tree present: {directory}")
    for directory in ("sim", "sw", "include"):
        for path in (root / directory).rglob("*"):
            if path.suffix in {".sv", ".svh"}:
                errors.append(f"RTL is paused: {path.relative_to(root)}")
    for path in [root / "CMakeLists.txt", root / "CMakePresets.json", *root.glob(".github/workflows/*")]:
        if re.search(r"gem5|verilat|npu_v[12]_|HOLON_NPU_V[12]_", path.read_text(), re.IGNORECASE):
            errors.append(f"retired dependency/name in {path.relative_to(root)}")
    if build is not None:
        try:
            units = compilation_units(build)
            sources = {p.resolve() for d in ("sim", "sw", "tests") for p in (root / d).rglob("*.cpp")}
            if sources != units.keys():
                errors.append(f"unowned/missing C++ targets: {sorted(map(str, sources ^ units.keys()))}")
            for source, (_, args) in units.items():
                standards = [arg for arg in args if arg.startswith("-std=")]
                if not standards or standards[-1] not in {"-std=c++26", "-std=c++2c"}:
                    errors.append(f"{source}: effective standard must be non-extension C++26")
        except (ValueError, KeyError, OSError) as exc:
            errors.append(str(exc))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    errors = check(ROOT, args.build_dir.resolve() if args.build_dir else None)
    if errors:
        print("\n".join(errors))
        return 1
    print("Repository ownership, dependencies and local links passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
