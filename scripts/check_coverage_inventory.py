"""Require all production translation units and line coverage above 90%."""

import json
import sys
from pathlib import Path


def main(report: Path) -> int:
    root = Path(__file__).resolve().parents[1]
    sources = {
        path.relative_to(root).as_posix()
        for path in (root / "src").rglob("*")
        if path.is_file() and path.suffix in {".c", ".cc", ".cpp", ".cxx"}
    }
    files = json.loads(report.read_text(encoding="utf-8"))["files"]
    lines_by_file = {
        entry["file"]: [line for line in entry["lines"] if not line.get("gcovr/excluded", False)]
        for entry in files
        if entry["file"].startswith("src/")
    }
    measured = {name for name, lines in lines_by_file.items() if lines}

    if not sources:
        print("No production sources found under src/.", file=sys.stderr)
        return 1
    missing = sources - measured
    if missing:
        print("Missing from coverage: " + ", ".join(sorted(missing)), file=sys.stderr)
        return 1
    print(f"Coverage report contains all {len(sources)} production translation units.")

    lines = [line for file_lines in lines_by_file.values() for line in file_lines]
    covered = sum(line["count"] > 0 for line in lines)
    total = len(lines)
    print(f"Line coverage: {covered / total:.2%} ({covered}/{total})")
    if covered * 100 <= 90 * total:
        print("Line coverage must be greater than 90%.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1])))
