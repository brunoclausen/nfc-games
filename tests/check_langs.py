#!/usr/bin/env python3
"""Check lang/*.txt share keys and that C++ t("key") exists in en.txt."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "lang")


def keys(path: pathlib.Path) -> list[str]:
    out: list[str] = []
    lines = path.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("#") or not line.strip():
            i += 1
            continue
        if "<<<" in line:
            out.append(line.split("<<<", 1)[0].strip())
            i += 1
            while i < len(lines) and lines[i].strip() != "<<<":
                i += 1
            i += 1
            continue
        if "=" in line:
            out.append(line.split("=", 1)[0].strip())
        i += 1
    return out


def main() -> int:
    en = keys(root / "en.txt")
    print(f"EN_KEYS {len(en)}")
    ok = True
    for code in ["da", "en", "de", "sv", "nb", "fr"]:
        p = root / f"{code}.txt"
        if not p.is_file():
            print(f"MISSING {code}.txt")
            ok = False
            continue
        got = keys(p)
        missing = [k for k in en if k not in got]
        extra = [k for k in got if k not in en]
        if missing or extra:
            ok = False
            if missing:
                print(f"MISSING_IN_{code} {','.join(missing)}")
            if extra:
                print(f"EXTRA_IN_{code} {','.join(extra)}")
        else:
            print(f"MATCH {code} {len(got)}")
    used: set[str] = set()
    cpp_dir = root.parent / "cpp"
    for cpp in cpp_dir.glob("*.cpp"):
        used.update(
            re.findall(r'\bt\("([a-z0-9_]+)"\)', cpp.read_text(encoding="utf-8", errors="replace"))
        )
    missing_used = sorted(k for k in used if k not in en)
    if missing_used:
        print("CPP_MISSING " + ",".join(missing_used))
        ok = False
    else:
        print(f"CPP_KEYS {len(used)}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
