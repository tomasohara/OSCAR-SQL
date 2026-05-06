#!/usr/bin/env python3
"""
Import BUG_FIXES.md entries as closed GitLab issues.

Usage:
    python Notes/import_bugs_to_gitlab.py --token YOUR_GITLAB_PAT [--dry-run]

Requires a GitLab personal access token with `api` scope.
Create one at: https://gitlab.com/-/user_settings/personal_access_tokens

What it does:
  - Fixed bugs       → closed issue, labels: bug, fixed
  - Known issues     → open issue, labels: bug, known-issue
  - Strikethrough    → skipped entirely
"""

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request

GITLAB   = "https://gitlab.com"
PROJECT  = "Seeker4%2FOSCAR-code"
API_BASE = f"{GITLAB}/api/v4/projects/{PROJECT}"

LABELS = [
    ("bug",         "#ee0701", "A confirmed defect"),
    ("fixed",       "#0075ca", "Defect has been resolved"),
    ("known-issue", "#e4e669", "Acknowledged; no fix planned yet"),
]


# ---------------------------------------------------------------------------
# GitLab API helpers
# ---------------------------------------------------------------------------

def _request(token: str, method: str, url: str, body: dict | None = None) -> dict:
    data = json.dumps(body).encode() if body else None
    req  = urllib.request.Request(
        url,
        data=data,
        headers={"PRIVATE-TOKEN": token, "Content-Type": "application/json"},
        method=method,
    )
    with urllib.request.urlopen(req) as r:
        return json.loads(r.read())


def ensure_labels(token: str, dry_run: bool) -> None:
    for name, color, desc in LABELS:
        if dry_run:
            print(f"  [dry] ensure label '{name}'")
            continue
        try:
            _request(token, "POST", f"{API_BASE}/labels",
                     {"name": name, "color": color, "description": desc})
            print(f"  Created label '{name}'")
        except urllib.error.HTTPError as e:
            if e.code == 409:
                pass  # already exists
            else:
                print(f"  Warning: could not create label '{name}': {e}")


def create_issue(token: str, title: str, body: str,
                 labels: list[str], close: bool, dry_run: bool) -> int | None:
    state = "closed" if close else "open"
    if dry_run:
        print(f"  [dry] {'[closed]' if close else '[open]  '} {title[:70]}")
        return None

    issue = _request(token, "POST", f"{API_BASE}/issues", {
        "title":       title,
        "description": body,
        "labels":      ",".join(labels),
    })
    iid = issue["iid"]

    if close:
        _request(token, "PUT", f"{API_BASE}/issues/{iid}", {"state_event": "close"})

    return iid


# ---------------------------------------------------------------------------
# BUG_FIXES.md parser
# ---------------------------------------------------------------------------

# Phrases that mark a "no fix" entry (kept open)
_NO_FIX_PHRASES = [
    "no fix applied",
    "noted; no fix",
    "no code change required",
    "status: noted",
    "conclusion: oscar is displaying the data correctly",
]

# Phrases that mark a pure observation / note (skip entirely)
_SKIP_PHRASES = [
    "note: spo2",          # BMC pulse/SpO2 note — not a bug
]


def _strip_strikethrough(text: str) -> str:
    """Remove ~~...~~ spans from markdown text (inline or whole-line)."""
    return re.sub(r'~~.+?~~', '', text, flags=re.DOTALL)


def parse_md(path: str) -> list[dict]:
    text = open(path, encoding="utf-8").read()

    # Find every ## heading position
    heading_re  = re.compile(r'^(## .+)$', re.MULTILINE)
    heading_ms  = list(heading_re.finditer(text))

    entries = []

    for i, m in enumerate(heading_ms):
        raw_header = m.group(1)[3:]  # strip leading "## "

        # --- skip wholly-struck headings ---
        if raw_header.startswith("~~"):
            continue

        # --- extract body (text until next heading or end of file) ---
        body_start = m.end()
        body_end   = heading_ms[i + 1].start() if i + 1 < len(heading_ms) else len(text)
        body = text[body_start:body_end].strip().rstrip("-").strip()

        # --- skip observation-only entries ---
        body_lower = body.lower()
        if any(p in body_lower for p in _SKIP_PHRASES):
            print(f"  Skipping (observation): {raw_header[:70]}")
            continue

        # --- determine open vs closed ---
        no_fix = any(p in body_lower for p in _NO_FIX_PHRASES)

        # --- build issue title (strip date prefix) ---
        header_clean = re.sub(r'~~', '', raw_header).strip()
        dm = re.match(r'\d{4}-\d{2}-\d{2}\s*[-—–]\s*(.+)', header_clean)
        title = dm.group(1).strip() if dm else header_clean

        # --- build issue body ---
        date_str = header_clean.split(' ')[0] if header_clean else ''
        issue_body = f"**Date fixed:** {date_str}\n\n{body}"

        entries.append({
            "title":  title,
            "body":   issue_body,
            "fixed":  not no_fix,
        })

    return entries


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    # Force UTF-8 console output on Windows (cp1252 can't print arrows, etc.)
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--token",   required=True, help="GitLab personal access token (api scope)")
    ap.add_argument("--md",      default="Notes/BUG_FIXES.md", help="Path to BUG_FIXES.md")
    ap.add_argument("--dry-run", action="store_true", help="Preview only; make no API calls")
    args = ap.parse_args()

    entries = parse_md(args.md)
    print(f"Parsed {len(entries)} entries from {args.md}")
    fixed_count  = sum(1 for e in entries if e["fixed"])
    open_count   = sum(1 for e in entries if not e["fixed"])
    print(f"  -> {fixed_count} to close as fixed, {open_count} to leave open as known-issue")
    print()

    print("Ensuring labels exist...")
    ensure_labels(args.token, args.dry_run)
    print()

    print("Creating issues...")
    created = 0
    for i, e in enumerate(entries):
        labels = ["bug", "fixed" if e["fixed"] else "known-issue"]
        try:
            iid = create_issue(
                token   = args.token,
                title   = e["title"][:255],
                body    = e["body"],
                labels  = labels,
                close   = e["fixed"],
                dry_run = args.dry_run,
            )
            created += 1
            if iid is not None:
                state = "closed" if e["fixed"] else "open"
                print(f"  #{iid:>3}  [{state}]  {e['title'][:65]}")
        except urllib.error.HTTPError as ex:
            body_bytes = ex.read()
            print(f"  ERROR on entry {i+1} ({e['title'][:50]}): HTTP {ex.code} — {body_bytes.decode()[:200]}")
        except Exception as ex:
            print(f"  ERROR on entry {i+1} ({e['title'][:50]}): {ex}")

        if not args.dry_run:
            time.sleep(0.35)  # stay well within GitLab rate limits

    print()
    print(f"Done. {'Would have created' if args.dry_run else 'Created'} {created} issues.")


if __name__ == "__main__":
    main()
