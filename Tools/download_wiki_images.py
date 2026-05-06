#!/usr/bin/env python3
"""
download_wiki_images.py — Download images referenced in wiki pages for backup.

USAGE
-----
  python tools/download_wiki_images.py --wiki-url URL [options]

Scans all .mediawiki files under oscar/wiki/ and downloads every referenced
image to oscar/wiki/images/ (gitignored — binary files, not version-controlled).

HOW URLS ARE RESOLVED
---------------------
MediaWiki stores images at a predictable URL derived from the MD5 hash of the
normalised filename:

  {wiki}/images/{md5[0]}/{md5[:2]}/{filename}

e.g.  https://www.apneaboard.com/wiki/images/7/78/OSCAR_2.0_Migration_Choice.png

The script computes these URLs locally with no network calls.

CLOUDFLARE
----------
The script first attempts a plain HTTP download for each image.  Static image
files at /wiki/images/ are often not protected by CloudFlare even when wiki
pages are.  If any images fail (403 / 403 / connection error), the script
falls back to downloading them through your real Brave browser via CDP, which
CloudFlare cannot distinguish from normal browsing.

Already-downloaded images are skipped, so re-running is safe and fast.

REQUIRES
--------
  pip install playwright
  python -m playwright install chromium   (only for the Brave CDP fallback)

OPTIONS
-------
  --wiki-url URL       Wiki base URL, e.g. https://www.apneaboard.com/wiki
                       (required)
  --wiki-dir PATH      Directory with .mediawiki files (default: oscar/wiki/)
  --out-dir PATH       Image output directory (default: oscar/wiki/images/)
  --brave-path PATH    Path to brave.exe (auto-detected if omitted)
  --browser-only       Skip direct download attempt; go straight to Brave
  --dry-run            List URLs that would be fetched without saving anything
"""

import argparse
import hashlib
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import urllib.error
from pathlib import Path

_REPO_ROOT    = Path(__file__).resolve().parent.parent
_DEFAULT_WIKI = _REPO_ROOT / "oscar" / "wiki"
_DEFAULT_IMGS = _DEFAULT_WIKI / "images"
_PROFILE_DIR  = Path(__file__).resolve().parent / ".chromium_profile"
_CDP_PORT     = 9222

_FILE_REF_RE  = re.compile(r'\[\[(?:File|Image):([^\]|#]+)', re.IGNORECASE)

_BRAVE_CANDIDATES = [
    Path(r"C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe"),
    Path(r"C:\Program Files (x86)\BraveSoftware\Brave-Browser\Application\brave.exe"),
    Path.home() / "AppData/Local/BraveSoftware/Brave-Browser/Application/brave.exe",
]

_DIRECT_HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                  "AppleWebKit/537.36 (KHTML, like Gecko) "
                  "Chrome/124.0.0.0 Safari/537.36",
    "Accept": "image/avif,image/webp,image/apng,image/*,*/*;q=0.8",
}


# ---------------------------------------------------------------------------
# URL computation
# ---------------------------------------------------------------------------

def normalize_filename(filename: str) -> str:
    """Apply MediaWiki filename normalisation: spaces → underscores, first char uppercase."""
    name = filename.replace(" ", "_")
    return name[0].upper() + name[1:] if name else name


def compute_image_url(wiki_base: str, filename: str) -> str:
    """
    Compute the direct download URL for a MediaWiki image file.
    Formula: {wiki_base}/images/{md5[0]}/{md5[:2]}/{encoded_filename}
    """
    name = normalize_filename(filename)
    md5  = hashlib.md5(name.encode("utf-8")).hexdigest()
    return f"{wiki_base}/images/{md5[0]}/{md5[:2]}/{urllib.parse.quote(name)}"


# ---------------------------------------------------------------------------
# Collect filenames from wikitext
# ---------------------------------------------------------------------------

def collect_filenames(wiki_dir: Path) -> list[str]:
    names: set[str] = set()
    for f in wiki_dir.rglob("*.mediawiki"):
        text = f.read_text(encoding="utf-8")
        for m in _FILE_REF_RE.finditer(text):
            name = m.group(1).strip()
            if name:
                names.add(name)
    return sorted(names)


# ---------------------------------------------------------------------------
# Direct download (no browser)
# ---------------------------------------------------------------------------

def try_direct(url: str) -> bytes | None:
    """Attempt a plain HTTP download. Returns bytes on success, None on any error."""
    try:
        req = urllib.request.Request(url, headers=_DIRECT_HEADERS)
        with urllib.request.urlopen(req, timeout=30) as resp:
            if resp.status == 200:
                return resp.read()
    except Exception:
        pass
    return None


# ---------------------------------------------------------------------------
# Browser download via Brave CDP (fallback)
# ---------------------------------------------------------------------------

def find_brave() -> Path | None:
    return next((p for p in _BRAVE_CANDIDATES if p.exists()), None)


def wait_for_real_page(page) -> str:
    """Wait until the page is no longer a CloudFlare challenge. Returns title."""
    while True:
        try:
            page.wait_for_load_state("load", timeout=30_000)
        except Exception:
            pass
        title = page.title()
        low   = title.lower()
        if not title or "just a moment" in low or "attention required" in low \
                or ("cloudflare" in low and "wiki" not in low):
            print(f"\n  CloudFlare challenge ('{title}') — complete it in the "
                  "Brave window, then press Enter here...", end="", flush=True)
            input()
            time.sleep(2)
        else:
            return title


def browser_download(page, url: str, out_path: Path) -> int:
    """Download an image by navigating to it in Brave and capturing the response."""
    with page.expect_response(
        lambda r: r.url.split("?")[0] == url.split("?")[0],
        timeout=30_000,
    ) as resp_info:
        page.goto(url, wait_until="commit", timeout=30_000)
    data = resp_info.value.body()
    out_path.write_bytes(data)
    return len(data)


def download_via_brave(brave_exe: Path, url_map: dict[str, str],
                       out_dir: Path) -> dict[str, str]:
    """
    Launch Brave, let the user pass CloudFlare, then download all images in
    url_map.  Returns {filename: error_message} for any that failed.
    """
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("Error: playwright required for browser fallback.  Run:", file=sys.stderr)
        print("  pip install playwright && python -m playwright install chromium",
              file=sys.stderr)
        return {name: "playwright not installed" for name in url_map}

    errors: dict[str, str] = {}
    _PROFILE_DIR.mkdir(parents=True, exist_ok=True)

    proc = subprocess.Popen([
        str(brave_exe),
        f"--remote-debugging-port={_CDP_PORT}",
        f"--user-data-dir={_PROFILE_DIR}",
        "--no-first-run",
        "--no-default-browser-check",
        list(url_map.values())[0],   # open first image URL directly
    ])

    with sync_playwright() as p:
        browser = None
        for _ in range(20):
            try:
                browser = p.chromium.connect_over_cdp(f"http://localhost:{_CDP_PORT}")
                break
            except Exception:
                time.sleep(1)
        if browser is None:
            proc.terminate()
            return {name: "could not connect to Brave" for name in url_map}

        context = browser.contexts[0] if browser.contexts else None
        if not context:
            proc.terminate()
            return {name: "no browser context" for name in url_map}

        page = context.pages[0] if context.pages else context.new_page()

        print("\nBrave is open. If a CloudFlare challenge appears, complete it.")
        input("Press Enter when you can see the image (or any wiki page)... ")
        wait_for_real_page(page)

        for filename, url in url_map.items():
            out_path = out_dir / filename
            print(f"  [browser dl] {filename} ...", end=" ", flush=True)
            try:
                size = browser_download(page, url, out_path)
                print(f"{size:,} bytes")
            except Exception as e:
                print(f"FAILED ({e})")
                errors[filename] = str(e)
            time.sleep(0.3)

        context.close()

    proc.terminate()
    return errors


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wiki-url",    required=True)
    parser.add_argument("--wiki-dir",    default=None)
    parser.add_argument("--out-dir",     default=None)
    parser.add_argument("--brave-path",  default=None)
    parser.add_argument("--browser-only", action="store_true",
                        help="Skip direct download; go straight to Brave")
    parser.add_argument("--dry-run",     action="store_true")
    args = parser.parse_args()

    wiki_dir  = Path(args.wiki_dir).resolve() if args.wiki_dir else _DEFAULT_WIKI
    out_dir   = Path(args.out_dir).resolve()  if args.out_dir  else _DEFAULT_IMGS
    wiki_base = args.wiki_url.rstrip("/")

    if not wiki_dir.exists():
        print(f"Error: wiki directory not found: {wiki_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Scanning {wiki_dir} for image references ...")
    filenames = collect_filenames(wiki_dir)
    if not filenames:
        print("No [[File:]] or [[Image:]] references found — nothing to do.")
        return
    print(f"Found {len(filenames)} unique image references.\n")

    if not args.dry_run:
        out_dir.mkdir(parents=True, exist_ok=True)

    # Compute all image URLs up front
    url_map = {f: compute_image_url(wiki_base, f) for f in filenames}

    if args.dry_run:
        for filename, url in url_map.items():
            print(f"  {filename}")
            print(f"    → {url}")
        return

    # --- Phase 1: direct download (no browser) ---
    need_browser: dict[str, str] = {}
    skipped = direct_ok = 0

    if not args.browser_only:
        print("Phase 1: direct download (no browser) ...")
        for filename, url in url_map.items():
            out_path = out_dir / filename
            if out_path.exists():
                print(f"  [skip]   {filename}")
                skipped += 1
                continue
            print(f"  [fetch]  {filename} ...", end=" ", flush=True)
            data = try_direct(url)
            if data:
                out_path.write_bytes(data)
                print(f"{len(data):,} bytes")
                direct_ok += 1
            else:
                print("blocked — queued for browser")
                need_browser[filename] = url
    else:
        need_browser = {f: u for f, u in url_map.items()
                        if not (out_dir / f).exists()}

    # --- Phase 2: browser download for anything that failed ---
    browser_ok = browser_failed = 0

    if need_browser:
        print(f"\nPhase 2: {len(need_browser)} image(s) need Brave browser ...")
        brave = Path(args.brave_path) if args.brave_path else find_brave()
        if not brave or not brave.exists():
            print("Error: Brave not found.  Install Brave or pass --brave-path.",
                  file=sys.stderr)
            browser_failed = len(need_browser)
        else:
            print(f"Using Brave: {brave}")
            errors = download_via_brave(brave, need_browser, out_dir)
            browser_failed = len(errors)
            browser_ok     = len(need_browser) - browser_failed
            for name, err in errors.items():
                print(f"  FAILED: {name} — {err}")

    print()
    total_ok = direct_ok + browser_ok
    print(f"  Downloaded: {total_ok}  "
          f"(direct: {direct_ok}, browser: {browser_ok})  "
          f"Skipped: {skipped}", end="")
    if browser_failed:
        print(f"  Failed: {browser_failed}", end="")
    print()


if __name__ == "__main__":
    main()
