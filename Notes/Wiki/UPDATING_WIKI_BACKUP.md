# Updating the Wiki Backup

This directory contains a backup copy of the OSCAR and OSCAR2 pages from the
ApneaBoard wiki.  Because we do not own the wiki hosting, these files are kept
here for emergency recovery.

**Wiki URL:** https://www.apneaboard.com/wiki

---

## Part A — Download wiki page text

Wiki pages are stored as `.mediawiki` files under `oscar/wiki/OSCAR/` and
`oscar/wiki/OSCAR2/`.  They are version-controlled in git.

### Tools needed
- A web browser (Brave, Chrome, etc.)
- Python 3 (standard library only — no extra packages)

### Steps

1. **Open Special:Export in your browser**

   Navigate to:
   ```
   https://www.apneaboard.com/wiki/Special:Export
   ```
   CloudFlare will let you through because you are using a real browser.

2. **Add the OSCAR categories**

   - In the *"Add pages from category"* box, type `OSCAR` and click **Add**.
   - Repeat for `OSCAR2`.
   - The text box will fill with page titles from both categories.

3. **Configure the export**

   - Tick **"Include only the current revision, not the full history"**
     (keeps the file small).
   - Leave everything else at defaults.

4. **Export and save**

   Click **Export**.  Save the downloaded XML file anywhere convenient
   (e.g. your Downloads folder).

5. **Run the parser**

   From the repo root:
   ```
   python tools/parse_export.py path\to\downloaded-export.xml
   ```
   The script writes one `.mediawiki` file per page into `oscar/wiki/OSCAR/`
   and `oscar/wiki/OSCAR2/`. 
   
6. **Commit**

   ```
   git add oscar/wiki/
   git commit -m "Update wiki pages from Special:Export"
   git push origin master
   ```

---

## Part B — Download images

Images are **not** stored in git (they are binaries and gitignored).
They live in `oscar/wiki/images/` on your local machine only.
Keep a separate backup of that folder (e.g. zip it to a cloud drive).

### Tools needed
- Python 3
- `playwright` Python package and its Chromium browser
- Brave browser (only needed if direct download is blocked by CloudFlare)

### One-time setup

```
pip install playwright
python -m playwright install chromium
```

### Steps

1. **Make sure the wiki page text is up to date** (Part A above), because
   the image list is derived from `[[File:...]]` references in the `.mediawiki`
   files.

2. **Run the image downloader**

   From the repo root:
   ```
   python tools/download_wiki_images.py --wiki-url https://www.apneaboard.com/wiki
   ```

3. **Phase 1 — direct download (automatic)**

   The script computes each image's URL from an MD5 hash of its filename
   (standard MediaWiki formula) and attempts a plain HTTP download.
   Static image files at `/wiki/images/` are often not CloudFlare-protected,
   so this phase may complete without opening a browser at all.

4. **Phase 2 — browser download (if needed)**

   Any images that were blocked in Phase 1 are retried through your real
   Brave browser via a remote-debugging connection.  If Phase 2 runs:

   - A Brave window opens automatically.
   - If a CloudFlare challenge appears (checkbox), complete it in the
     Brave window.
   - Switch back to the terminal and press **Enter**.
   - The script downloads the remaining images through that verified
     Brave session and then closes the window.

   > **Note:** `tools/.chromium_profile/` stores the CloudFlare session
   > cookies between runs.  If you run the script again within ~24 hours
   > the challenge may not appear at all.

5. **Verify and back up**

   After the script finishes, `oscar/wiki/images/` will contain all the
   image files.  Zip or otherwise back up this folder — it is gitignored
   and exists only on the machine where the script was run.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `No [[File:]] references found` | `.mediawiki` files missing | Run Part A first |
| Phase 1 blocked for all images | CloudFlare on images path | Phase 2 (Brave) handles it |
| Brave fails to connect on port 9222 | Previous run left Brave running | Kill leftover `brave.exe` in Task Manager |
| `Brave not found` | Non-standard install path | Pass `--brave-path "C:\path\to\brave.exe"` |
| Page stuck on CloudFlare in Brave | Need to click checkbox | Complete challenge in Brave window, then press Enter in terminal |
| Image in `uncategorized/` folder | Category tag is inside a template | Move file to `OSCAR/` or `OSCAR2/` manually |
