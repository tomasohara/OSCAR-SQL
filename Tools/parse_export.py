#!/usr/bin/env python3
"""
parse_export.py — Import a MediaWiki Special:Export XML into the repo.

USAGE
-----
  python tools/parse_export.py <export.xml>

HOW TO GET THE EXPORT FILE
--------------------------
1. Open your wiki's Special:Export page in a browser (CloudFlare is handled
   automatically because you are using a real browser).
2. In the "Add pages from category" box, type "OSCAR" and click Add.
3. Repeat for "OSCAR2".
4. Tick "Include only the current revision, not the full history".
5. Click Export and save the downloaded XML file anywhere convenient.
6. Run this script pointing at that file.

OUTPUT
------
  oscar/wiki/OSCAR/          — pages tagged [[Category:OSCAR]] or {{OSCAR}}
  oscar/wiki/OSCAR2/         — pages tagged [[Category:OSCAR2]] or {{OSCAR2}}
  oscar/wiki/uncategorized/  — pages whose category cannot be detected from
                               raw wikitext; inspect and move manually.

Each page is written as <Page Title>.mediawiki.  If a page belongs to both
OSCAR and OSCAR2 it appears in both subdirectories (the files are small).

OPTIONS
-------
  --wiki-dir PATH   Override the output directory (default: oscar/wiki/ in
                    the repo root).
  --dry-run         Print what would be written without touching the filesystem.
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

CATEGORIES = ["OSCAR", "OSCAR2"]

# Default wiki output directory relative to the repo root (one level above tools/).
_REPO_ROOT = Path(__file__).resolve().parent.parent
_DEFAULT_WIKI_DIR = _REPO_ROOT / "oscar" / "wiki"


def detect_namespace(root: ET.Element) -> str:
    """Return the XML namespace URI from the root element tag, or ''."""
    m = re.match(r'\{([^}]+)\}', root.tag)
    return m.group(1) if m else ""


def sanitize_filename(title: str) -> str:
    """Convert a MediaWiki page title to a safe filesystem name."""
    # Characters forbidden on Windows (and generally unsafe)
    for ch in r'\/:*?"<>|':
        title = title.replace(ch, "_")
    # Collapse runs of underscores introduced by the replacement
    title = re.sub(r'_+', '_', title)
    return title.strip(". _")


def page_categories(wikitext: str) -> list[str]:
    """
    Return which of the known CATEGORIES this page belongs to.

    Detects two forms:
      [[Category:OSCAR]]   — explicit category tag in wikitext
      {{OSCAR}}            — template call that sets the category internally
                             (e.g. all OSCAR pages open with {{OSCAR2}})
    The category name must match exactly (OSCAR / OSCAR2).
    """
    found = []
    for cat in CATEGORIES:
        tag_pattern      = rf'\[\[\s*[Cc]ategory\s*:\s*{re.escape(cat)}\s*[\]|]'
        template_pattern = rf'\{{\{{\s*{re.escape(cat)}\s*[\}}|]'
        if re.search(tag_pattern, wikitext) or re.search(template_pattern, wikitext):
            found.append(cat)
    return found


def parse_and_write(xml_file: Path, wiki_dir: Path, dry_run: bool) -> None:
    print(f"Parsing {xml_file} ...")

    tree = ET.parse(xml_file)
    root = tree.getroot()

    mw_ns = detect_namespace(root)
    p = f"{{{mw_ns}}}" if mw_ns else ""   # namespace prefix for element lookups

    counts: dict[str, int] = {cat: 0 for cat in CATEGORIES}
    counts["uncategorized"] = 0
    counts["skipped_ns"] = 0

    for page_elem in root.iter(f"{p}page"):
        title_elem = page_elem.find(f"{p}title")
        ns_elem    = page_elem.find(f"{p}ns")

        if title_elem is None or title_elem.text is None:
            continue

        title = title_elem.text.strip()

        # Only export main-namespace pages (ns == 0).
        # Talk pages, File pages, Help pages, etc. are excluded.
        if ns_elem is not None and ns_elem.text != "0":
            counts["skipped_ns"] += 1
            continue

        # Grab wikitext from the most recent (or only) revision.
        revision = page_elem.find(f"{p}revision")
        if revision is None:
            continue
        text_elem = revision.find(f"{p}text")
        wikitext  = (text_elem.text or "") if text_elem is not None else ""

        cats = page_categories(wikitext)
        if not cats:
            cats = ["uncategorized"]

        filename = sanitize_filename(title) + ".mediawiki"

        for cat in cats:
            out_dir  = wiki_dir / cat
            out_path = out_dir / filename
            label    = f"[{cat}]"
            if dry_run:
                print(f"  {label:<18} {out_path.relative_to(wiki_dir.parent.parent)}")
            else:
                out_dir.mkdir(parents=True, exist_ok=True)
                out_path.write_text(wikitext, encoding="utf-8")
                print(f"  {label:<18} {title}")
            counts[cat] += 1

    # Summary
    print()
    if dry_run:
        print("DRY RUN — nothing written.")
    else:
        print("Done.")
    for cat in CATEGORIES:
        print(f"  {cat:<18} {counts[cat]} pages")
    if counts["uncategorized"]:
        print(f"  {'uncategorized':<18} {counts['uncategorized']} pages  "
              f"(category tag is in a template — check oscar/wiki/uncategorized/)")
    if counts["skipped_ns"]:
        print(f"  Skipped {counts['skipped_ns']} non-article pages "
              f"(Talk, File, Help, …)")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("xml_file", help="Path to MediaWiki export XML file")
    parser.add_argument(
        "--wiki-dir",
        default=None,
        help="Output directory (default: oscar/wiki/ in the repo root)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would be written; don't touch the filesystem",
    )
    args = parser.parse_args()

    xml_file = Path(args.xml_file).resolve()
    if not xml_file.exists():
        print(f"Error: file not found: {xml_file}", file=sys.stderr)
        sys.exit(1)

    wiki_dir = Path(args.wiki_dir).resolve() if args.wiki_dir else _DEFAULT_WIKI_DIR

    parse_and_write(xml_file, wiki_dir, args.dry_run)


if __name__ == "__main__":
    main()
