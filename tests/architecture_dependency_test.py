#!/usr/bin/env python3

import argparse
import re
from pathlib import Path


INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
CORE_DIRECTORIES = {
    "ChatService",
    "Command",
    "Memory",
    "Message",
    "ModelApiCaller",
    "Network",
    "Tool",
    "UserSession",
}
CONFIGURATION_FORBIDDEN_TARGETS = {
    "ChatService",
    "Memory",
    "Message",
    "ModelApiCaller",
    "Network",
}


def included_module(include: str):
    parts = [part for part in include.replace("\\", "/").split("/") if part not in {"", ".", ".."}]
    if "src" in parts:
        parts = parts[parts.index("src") + 1 :]
    return parts[0] if parts else None


def scan(source_root: Path):
    violations = []
    for path in sorted(source_root.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp"}:
            continue

        relative = path.relative_to(source_root)
        owner = relative.parts[0] if len(relative.parts) > 1 else None
        for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            match = INCLUDE_PATTERN.match(line)
            if not match:
                continue

            target = included_module(match.group(1))
            if owner in CORE_DIRECTORIES and target == "Configuration":
                violations.append(
                    f"{relative}:{line_number}: core module {owner} must not include Configuration"
                )
            if owner == "Configuration" and target in CONFIGURATION_FORBIDDEN_TARGETS:
                violations.append(
                    f"{relative}:{line_number}: Configuration must not include {target}"
                )
    return violations


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()

    violations = scan(args.source_root.resolve())
    if violations:
        print("Architecture dependency violations:")
        for violation in violations:
            print(f"- {violation}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
