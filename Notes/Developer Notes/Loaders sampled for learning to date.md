  1. BMC G3 A20 (G3X / new "BMC G/E/P INDEX" platform) — bmcg3x_loader
  2. BMC Luna G3 ("G2" platform / legacy, with *.USR sentinel) — bmc_loader
  3. ResMed AirCurve 10 VAuto (AS10 platform, bilevel) — resmed_loader
  4. ResMed AirSense 10 CPAP (AS10 platform, basic CPAP — summary-only model) — resmed_loader
  5. Philips Respironics DreamStation 2 (PRS1 model 410X150C, encrypted PROP.BIN) — prs1_loader
        6. Philips Respironics DreamStation Go Auto (PRS1 model 500G150 + older 500G110 on same card, cleartext PROP.TXT) — prs1_loader
  7. Yuwell YH690F (Format D, flow-recording) — yuwell_loader
  8. Yuwell YH-550A (Format A, summary-only) — yuwell_loader
  9. Yuwell YH-680B (Format D, summary-only) — yuwell_loader

Plus the AeonMed AS100 Auto from the earlier session, written up separately in Notes/AEONMED_AS100_CARD_ANALYSIS.md (not yet in the fingerprints doc).

  **Loaders not yet sampled** (from the CPAP-focused list we agreed on earlier):

  - Resmed:
      - ResMed AirSense 11 (AS11 — uses Identification.json instead of .tgt; would close the AS10/AS11 comparison)
      - ResMed S9 (older platform — predates AS10's identity file; worth knowing if format diverges)

  - PRS1 System One / DreamStation 1 desktop (DS Go is closely related, but a true DS 1 desktop card would confirm the 500-series notes apply to the 560P/660P/760P 60-series too)
  - F&P Icon (FPHCARE/ICON/<digits>/SUM*.fph)
  - F&P SleepStyle (same FPHCARE/ICON/... skeleton with alphanumeric serial)
  - IntelliPap DV5 (SL/SET1)
      - IntelliPap DV6 (DV6/SET.BIN)
      - Löwenstein Prisma Smart (config.pscfg)
      - Löwenstein Prisma Line (config.pcfg)
      - Resvent (THERAPY/CONFIG/ + THERAPY/RECORD/)

  - Weinmann SOMNO-series (WM_DATA.TDF)
  - VREM (VREM*/PI.txt + DI.txt)
      - Yuwell Format B (YH-580, single 64 KB YHSD-NEW.BYS at root)
      - Yuwell Format C (YH-830, MODEL-SERIAL/*.BYS with no RunLog.bys)

  - PRS1 BiPAP / ASV variants (different model-number prefixes — useful to know if file structure differs)

Anything in that list you have samples for would be valuable. The ~~ResMed AS11 and~~ F&P Icon are probably the biggest gaps — both are common devices with structurally distinct identity files.