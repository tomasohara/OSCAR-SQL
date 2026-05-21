# OSCAR Loader Detection Patterns

How OSCAR identifies CPAP SD cards and what layout characteristics distinguish one
format from another. Useful when evaluating a new SD card image: this lets you
decide whether an existing loader already handles a similar layout, or whether a
new loader is warranted, and which existing pattern is the best template.

Source files: `oscar/SleepLib/loader_plugins/*` plus the autoscanner in
`oscar/mainwindow.cpp:1219`.

---

## Dispatcher behaviour — what `Detect()` is actually answering

In `mainwindow.cpp:1219–1228`, the autoscanner walks every registered loader and
asks each one `loader->Detect(path)`. **There is no precedence and no
exclusivity** — every loader that returns `true` is appended to `detectedCards`.
This means:

- `Detect()` must be *specific enough* that two unrelated loaders never both
  claim the same card.
- When two loaders genuinely could match (variants of the same brand), one must
  explicitly defer. **The only example of this in the codebase is
  `bmcg3x_loader.cpp:27`**, which calls `BmcData::DirectoryHasBmcData()` and
  returns `false` if the legacy BMC loader would already match.

Three classes of loader exist:

- **SD-card loaders** (most) — `Detect(path)` does real filesystem checks.
- **File-based loaders** (somnopose, zeo, dreem, viatom, mseries) — return
  `false` from `Detect`; user picks files via dialog and `OpenFile()` parses
  them.
- **Serial-port oximeters** (cms50, cms50f37, md300w1) — return `true/false`
  based on a user preference (`p_profile->oxi->oximeterType()`), not the path.

---

## Per-loader detection sentinels

| Loader | Sentinel that identifies the card |
|---|---|
| **ResMed** (`resmed_loader.cpp:309`) | `DATALOG/` directory + `STR.edf` file at root |
| **PRS1** (`prs1_loader.cpp:572`) | `P-SERIES/` folder containing one or more subdirs that hold a `PROP*.TXT` or `PROP.BIN` |
| **F&P Icon** (`icon_loader.cpp:146`) | `FPHCARE/ICON/<serial>/SUM*.fph` (resolves through `getIconDir2`) |
| **F&P SleepStyle** (`sleepstyle_loader.cpp:145`) | Same `FPHCARE/ICON/<serial>/SUM*.fph` structure — collision with Icon, currently both claim it |
| **IntelliPap** (`intellipap_loader.cpp:60`) | `SL/SET1` (DV5/DV54) **or** `DV6/SET.BIN` (DV6/DV64) |
| **Weinmann** (`weinmann_loader.cpp:50`) | `WM_DATA.TDF` at root |
| **Prisma** (`prisma_loader.cpp:552`) | `config.pscfg` (Prisma Smart) **or** `config.pcfg` (Prisma Line) at root |
| **Resvent** (`resvent_loader.cpp:212`) | `THERAPY/CONFIG/` + `THERAPY/RECORD/` directories |
| **BMC legacy** (`bmc_loader.cpp:584` → `bmcDataParsing.cpp:465`) | Triplet by basename: `*.USR` + `*.idx` + `*.000`; exactly one `.USR` file in the directory |
| **BMC G3X** (`bmcg3x_loader.cpp:19`) | Any `*.idx` whose first 32 bytes start with magic `"BMC G/E/P INDEX"`, with a sibling `*.000`. Defers to legacy BMC if `*.USR` triplet also matches |
| **VREM** (`vrem_loader.cpp:66`) | Directory whose name starts with `VREM` (case-insensitive) containing both `PI.txt` and `DI.txt`. Searches one level down too |
| **Yuwell** (`yuwell_loader.cpp:1549`) | Factory dispatches to A → B → C → D; first match wins (see below) |

Yuwell's four sub-formats illustrate intra-brand variation:

- **A** (YH-550): `RunLog.bys` (0-byte sentinel) at root + a `MODEL-SERIAL/`
  subdir holding `*.BYS`.
- **B** (YH-580): single `YHSD-NEW.BYS` at root, **exactly 64 KB** (size used as
  a discriminator).
- **C** (YH-830): `MODEL-SERIAL/*.BYS` with no `RunLog.bys` marker —
  distinguishes from A by absence of A's marker.
- **D** (YH-680/690 "BreathCare III"): `MODEL-SERIAL/00xxxxxx/0xxxxxxxm.BYS` +
  `0xxxxxxxs.BYS` two-level structure.

---

## Six abstract detection strategies

These reduce to six general strategies you can match against a new card:

1. **Fixed sentinel file at root** — Weinmann (`WM_DATA.TDF`), Prisma
   (`config.pscfg`/`config.pcfg`), ResMed (`STR.edf` + `DATALOG/`).
2. **Sentinel sub-folder with required contents** — IntelliPap (`SL/` or
   `DV6/` + their config file), Resvent (`THERAPY/CONFIG`+`/RECORD`), PRS1
   (`P-SERIES/*/PROP*.TXT`), F&P (`FPHCARE/ICON/<serial>/SUM*.fph`).
3. **Same-basename file triplet** — BMC legacy `*.USR` + `*.idx` + `*.000`.
   Lightweight but fragile (any directory with those extensions would match).
4. **File-content magic** — BMC G3X reads the first 32 bytes of every `*.idx`
   looking for `"BMC G/E/P INDEX"`. Strictest method; least likely to
   false-positive. Viatom also uses 2-byte LE signatures at file open
   (`0x0003/0x0005/0x0006/0x0301`) but only in `OpenFile`.
5. **Directory-name prefix + content** — VREM (dir starts with `VREM` +
   `PI.txt`/`DI.txt`).
6. **Factory of sub-formats** — Yuwell tries A,B,C,D in order. Useful when one
   brand has incompatible firmware generations.

---

## Additional layout characteristics worth recording for a new card

Beyond what `Detect()` checks, these traits drive how parsing maps to OSCAR's
day/session/event model:

- **Where the serial number lives.** Embedded in identity file
  (ResMed `Identification.json`/`.tgt`, PRS1 `PROP.TXT`, BMC IDX header
  offset 0x30, Yuwell C/D in file body), encoded as a directory name
  (F&P Icon/SleepStyle, Yuwell A `MODEL-SERIAL`, Viatom folder name when
  ≥9 chars), or absent (Viatom POD2 uses fake serial `"POD2"`).
- **Where model/firmware lives.** Same identity file as serial (ResMed, PRS1,
  BMC G3X offsets 0x100 and 0x345), separate sidecar log (BMC G3X uses companion
  `.log`), or implied by sub-format detection (Yuwell A/B/C/D each maps to a
  model family).
- **Data encoding.** Standard EDF/EDF+ (ResMed waveforms; SleepStyle uses EDF
  too), custom binary with basename triplets (BMC), text-keyed configs
  (IntelliPap, Resvent — line-oriented `Key=Value`), proprietary `.BYS` blocks
  (Yuwell), XML inside a TDF (Weinmann uses QDomDocument).
- **Time-series organization.** Per-day folder (ResMed `DATALOG/yyyymmdd/`),
  per-device with a global index (BMC, Yuwell C/D), single monolithic file
  (Weinmann, IntelliPap, Viatom POD2), or chunked per-session waveform packets
  (BMC `.000` waveform crumbs, PRS1 `.001`/`.002`/`.005` chunks).
- **Backup/import dual-path quirks.** Icon, SleepStyle, Yuwell, Prisma all
  check `if (ipath == bpath) rebuild_from_backups = true` so re-importing
  OSCAR's own backup folder doesn't recurse-copy.
- **Filesystem case sensitivity.** PRS1 uppercases-and-compares for
  `P-Series`/`P-SERIES`/`p-series`. Several others (BMC, VREM) iterate
  `entryList` directly so depend on the OS. Worth checking on a new format.

---

## How to position a new card against this set

When inspecting an unknown SD card, ask in this order:

1. **Is there a single distinctive file at the root?**
   (`STR.edf` → ResMed; `WM_DATA.TDF` → Weinmann;
   `config.pscfg`/`config.pcfg` → Prisma; `*.USR` → BMC legacy.)
2. **Is there a fixed sub-folder?**
   (`P-Series/` → PRS1; `FPHCARE/ICON/` → F&P; `SL/` or `DV6/` → IntelliPap;
   `THERAPY/` → Resvent.)
3. **Is there a same-basename multi-extension set?**
   (`.idx + .000` with BMC magic → BMC G3X; `.USR + .idx + .000` → BMC legacy.)
4. **Are there `MODEL-SERIAL`-named directories?**
   (Yuwell territory; check for `RunLog.bys`, `YHSD-NEW.BYS` size = 64 KB,
   two-level digits.)
5. **Does the directory name itself encode the brand?** (`VREM*`.)
6. **Does a file header have a magic signature?**
   (Always preferred over filename-only matching — see BMC G3X.)

If a card matches **any sentinel an existing loader uses**, the new loader must
either be uniquely specific or explicitly defer to the overlapping loader
(BMC G3X-style) — otherwise OSCAR will report ambiguous matches.
