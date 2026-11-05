#!/usr/bin/env python3
"""Fails when the client's own sources name a custom server outside comments.

Usage: tools/check_core_names.py [repo root]

A server's client changes are a plugin (plugins/<name>, docs/plugins.md), never
code in the client. This scans src/, sdk/ and ui/ (C, C++ and Lua) with the
comments removed and reports every identifier or string that names NMS. The
older `nmsc` / `NMSC_` names (the namespace, the targets, the SDK prefix) and
`nms-client` (the project's old name, for moving an old install's files) are
not the server and are allowed. CI runs it; it exits 1 when it finds any.

One exception, by file and exact line shape (ALLOWED below): the updater's table of files the old releases
shipped under plugins/nms/ (src/app/update_install.cpp, retired_release_files()). Those entries are a path,
a size and a SHA-256 of a published asset, used to delete exactly those files from an old install; they are
not a server's client code, and anything else naming NMS in that file still fails.
"""

import re
import sys
from pathlib import Path

DIRS = ("src", "sdk", "ui")
C_EXT = {".c", ".cc", ".cpp", ".h", ".hpp", ".inc"}
LUA_EXT = {".lua"}
# "nms" as a word or a word's start (kOpNms, encode_nms), but not "nmsc" or "nms-client" (any case).
NAME = re.compile(r"(?<![A-Za-z0-9])[Nn][Mm][Ss](?![Cc]|-client)|(?<=[a-z0-9])(?:Nms|NMS)(?![Cc])")

# File -> pattern a whole (stripped) line must match to be let through. Keep this narrow: one table of data.
ALLOWED = {
    "src/app/update_install.cpp": re.compile(r'\{"plugins/nms/[A-Za-z0-9_./-]+", [0-9]+, "[0-9a-f]{64}"\},'),
}


def strip_c(text: str) -> str:
    """Blanks // and /* */ comments, keeps strings and line numbers."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_lua(text: str) -> str:
    """Blanks -- and --[[ ]] comments, keeps strings and line numbers."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("--", i):
            m = re.match(r"--\[(=*)\[", text[i:])
            if m:
                close = "]" + m.group(1) + "]"
                j = text.find(close, i)
                j = n if j < 0 else j + len(close)
                out.append("\n" * text.count("\n", i, j))
            else:
                j = text.find("\n", i)
                j = n if j < 0 else j
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
    found = 0
    for d in DIRS:
        for path in sorted((root / d).rglob("*")):
            ext = path.suffix.lower()
            if ext in C_EXT:
                code = strip_c(path.read_text(encoding="utf-8", errors="replace"))
            elif ext in LUA_EXT:
                code = strip_lua(path.read_text(encoding="utf-8", errors="replace"))
            else:
                continue
            allowed = ALLOWED.get(path.relative_to(root).as_posix())
            for number, line in enumerate(code.splitlines(), 1):
                if allowed and allowed.fullmatch(line.strip()):
                    continue
                if NAME.search(line):
                    print(f"{path.relative_to(root)}:{number}: {line.strip()}")
                    found += 1
    if found:
        print(f"{found} line(s) name NMS outside comments: a server's client changes belong in its plugin "
              "(plugins/, docs/plugins.md)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
