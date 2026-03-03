#!/usr/bin/env python3
"""Generate oscar/exports/system_report_strings.cpp from oscar/docs/system_reports.orf.

Copyright (c) 2026 The OSCAR Team

This script parses system_reports.orf and extracts the folder names, report
names, and descriptions that need to be available to Qt Linguist (lupdate) for
translation.  Run it manually whenever system_reports.orf is changed:

    python3 Tools/gen_system_report_strings.py

from the OSCAR-code root directory (the directory that contains oscar/ and Tools/).
The output file is oscar/exports/system_report_strings.cpp.
"""

import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths (relative to the OSCAR-code root)
# ---------------------------------------------------------------------------
SCRIPT_DIR  = Path(__file__).resolve().parent
ROOT_DIR    = SCRIPT_DIR.parent
ORF_PATH    = ROOT_DIR / "oscar" / "docs" / "system_reports.orf"
OUT_PATH    = ROOT_DIR / "oscar" / "exports" / "system_report_strings.cpp"

# Root-node labels are not in the .orf file; list them here.
ROOT_NAMES  = ["System", "User"]

# ---------------------------------------------------------------------------
# Parse the .orf file
# ---------------------------------------------------------------------------

def parse_orf(path: Path):
    """Return (folders, report_names, descriptions) as ordered lists of unique strings."""
    folders      = []
    report_names = []
    descriptions = []

    seen_folders = set()
    seen_names   = set()
    seen_descs   = set()

    in_query = False

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()

        # Skip heredoc blocks (SQL queries) — nothing translatable there.
        if line == "<<SQL":
            in_query = True
            continue
        if line == "SQL":
            in_query = False
            continue
        if in_query:
            continue

        # === Folder: <name> ===
        m = re.fullmatch(r"===\s+Folder:\s+(.+?)\s+===", line)
        if m:
            name = m.group(1).strip()
            if name not in seen_folders:
                seen_folders.add(name)
                folders.append(name)
            continue

        # === Report: <folder>/<report name> ===
        m = re.fullmatch(r"===\s+Report:\s+.+?/(.+?)\s+===", line)
        if m:
            name = m.group(1).strip()
            if name not in seen_names:
                seen_names.add(name)
                report_names.append(name)
            continue

        # Description: <text>
        m = re.match(r"Description:\s+(.+)", line)
        if m:
            desc = m.group(1).strip()
            if desc not in seen_descs:
                seen_descs.add(desc)
                descriptions.append(desc)
            continue

    return folders, report_names, descriptions

# ---------------------------------------------------------------------------
# Emit the .cpp file
# ---------------------------------------------------------------------------

FILE_HEADER = """\
/* System Report Translation Strings
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file exists solely so that lupdate can extract system report
 * names and descriptions for translation via Qt Linguist. It produces
 * no runtime code. Keep in sync with docs/system_reports.orf.
 *
 * Usage:
 *   - Every folder name, report name, and description that appears in
 *     system_reports.orf must have a QT_TRANSLATE_NOOP entry here.
 *   - At display time, report_tree_model.cpp translates these strings
 *     using QCoreApplication::translate("SystemReports", ...).
 *   - User-created reports are NOT translated (displayed as-is).
 *
 * DO NOT EDIT BY HAND — regenerate with:
 *   python3 Tools/gen_system_report_strings.py
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QCoreApplication>
"""

def escape_cpp(s: str) -> str:
    """Escape backslashes and double-quotes for a C string literal."""
    return s.replace("\\", "\\\\").replace('"', '\\"')

def write_array(out, comment: str, var_name: str, strings: list):
    out.append(f"// ---- {comment} ----")
    out.append(f"static const char* const {var_name}[] = {{")
    for s in strings:
        out.append(f'    QT_TRANSLATE_NOOP("SystemReports", "{escape_cpp(s)}"),')
    out.append("};")
    out.append("")

def generate(folders, report_names, descriptions) -> str:
    out = [FILE_HEADER]
    write_array(out, "Root node names",   "roots",        ROOT_NAMES)
    write_array(out, "Folder names",      "folders",      folders)
    write_array(out, "Report names",      "names",        report_names)
    write_array(out, "Report descriptions", "descriptions", descriptions)
    return "\n".join(out)

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if not ORF_PATH.exists():
        sys.exit(f"ERROR: Cannot find {ORF_PATH}")

    folders, report_names, descriptions = parse_orf(ORF_PATH)

    if not folders and not report_names and not descriptions:
        sys.exit("ERROR: Nothing extracted from .orf file — check its format.")

    content = generate(folders, report_names, descriptions)
    OUT_PATH.write_text(content, encoding="utf-8")

    print(f"Written {OUT_PATH}")
    print(f"  {len(ROOT_NAMES)} root node(s)")
    print(f"  {len(folders)} folder(s)")
    print(f"  {len(report_names)} report name(s)")
    print(f"  {len(descriptions)} description(s)")

if __name__ == "__main__":
    main()
