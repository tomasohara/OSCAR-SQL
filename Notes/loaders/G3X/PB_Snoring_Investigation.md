# BMC G3X — Periodic Breathing & Snoring Investigation

**Date started:** 2026-03-16
**Waveform file:** `Notes/G3X/G3/SD card data/B33BF114508.000`
**EVT file:** `Notes/G3X/G3/SD card data/B33BF114508.evt`
**Night examined:** 2026-03-16 (00:27–09:04, 30,987 waveform packets)
**Note:** Screenshot from PAP-Link was taken in Macao — date may be one day ahead of stored data date.

---

## Channels Still Unidentified in OSCAR

The following channels appear in the BMC PAP-Link app but have not yet been sourced in the OSCAR G3X loader:

| Channel | User's guess |
|---------|-------------|
| SpO2 | Waveform data in .00x file |
| Pulse Rate | Waveform data in .00x file |
| Periodic Breathing (PB) | EVT event or computed |
| Snoring | EVT event |

---

## SpO2 and Pulse Rate

- `bmc_loader.cpp` already creates `wSpO2` and `wPulse` EventLists and calls `AddEvent` for `bmcWaveform.Raw.SpO2Pct` and `bmcWaveform.Raw.PulseRate`.
- However, `bmcG3xDataParsing.cpp` **never populates** `legacyPacket.Raw.SpO2Pct` or `legacyPacket.Raw.PulseRate` — those fields stay 0, so nothing gets plotted.
- The `BmcWaveformPacketStruct` offsets 0xCC (SpO2) and 0xCE (PulseRate) are for the **legacy** (non-G3X) format. The G3X uses a different 0x800-byte packet.
- SpO2/Pulse Rate offsets in the G3X 0x800-byte waveform packet have **not yet been found**.

---

## EVT File — 3/16/2026 Event Code Summary

| Code | Count | Status |
|------|-------|--------|
| `0x01` | 19 | Unknown; spread throughout night; doesn't cluster to match PAP screenshot |
| `0x03` | 30 | OSA (confirmed) |
| `0x04` | 20 | CSA (confirmed) |
| `0x09` | 2 | Hypopnea subtype (confirmed) |
| `0x0C` | 8308 | Leak (confirmed) |
| `0x0D` | 8308 | Unknown; mirrors 0x0C |
| `0x40` | 1 | Unknown; likely session marker |
| `0x41` | 1 | Unknown; likely session marker |
| `0x42` | 470 | Pressure wave (confirmed) |
| `0x43` | 43 | Flow limitation candidate; spread evenly throughout night — does not match PB or snoring pattern |
| `0x44` | 26 | Unknown; spread throughout night; doesn't cluster to match PAP screenshot |

**Note:** No `0x02`, `0x07`, `0x08`, `0x0E`, `0x0F` codes present in this night's data.

---

## Waveform Packet Offset Scan Results

### Scan 1 — Moderate sparsity (0.5%–40% non-zero), range 0x532–0x76A

Top candidates by two-cluster score — all found to correlate with **pressure increases** during PB episodes, not PB flags themselves:

| Offset | NZ% | Cluster 1 | Cluster 2 |
|--------|-----|-----------|-----------|
| 0x694–0x69C | 3.4% | 01:12 | 08:51 |
| 0x56A–0x56C | 12% | 00:39 | 09:04 |
| 0x544 | 26.8% | 02:21 | 06:41 |
| 0x534 | 16.1% | 02:21 | 06:41 |

**0x534** — constant value 1000 when non-zero (true binary flag), but fires ~every 6–7 seconds all night (3,668 active spans). This is a **per-breath timing marker**, not a PB episode flag. Plot appears as a solid blue rectangle at full-night scale.

### Scan 2 — Very sparse (2–150 non-zero packets), full packet range 0x058–0x7FE

**Zero candidates found.** No offset has 2–150 non-zero packets forming two clusters. Rules out a simple brief on/off episode flag anywhere in the waveform packet.

### Ranges not yet scanned

- `0x058–0x249` (between legacy pressure wave and mask pressure region)
- `0x30A–0x37F` (between mask pressure and pressure wave regions)
- Single-byte (uint8) interpretation not yet tried
- Bit-field interpretation not yet tried

---

## Hypothesis: PB May Be Computed, Not Stored

**Key question:** Does PAP-Link read PB from SD card data, or compute it from the flow waveform?
**Status:** Unknown. Neither the user nor the investigation has confirmed this.

### Options to resolve:

1. **Cross-reference EVT timestamps** — get a clearer PAP-Link screenshot with PB episode times and compare against 0x01 and 0x44 record timestamps.
2. **Test with no SD card** — if PAP-Link shows PB from Bluetooth live stream (without SD card), it is computed.
3. **Contact BMC** — unlikely to yield useful answer.
4. **Implement computed PB in OSCAR** — detect waxing/waning flow pattern algorithmically (ResMed OSCAR already has CSR/PB detection). If OSCAR results match PAP-Link, it was computed. **Recommended path** — useful regardless of whether BMC stores a flag.

---

## Scripts Produced

| Script | Purpose |
|--------|---------|
| `Notes/G3X/plot_offsets_54E_768.py` | Plot uint16 values at two specific offsets |
| `Notes/G3X/scan_waveform_sparse.py` | Scan 0x532–0x76A for moderate-sparsity two-cluster candidates |
| `Notes/G3X/scan_waveform_very_sparse.py` | Scan full packet for very sparse (2–150 nz) two-cluster candidates |
| `Notes/G3X/plot_sparse_candidates.py` | Stem/rug plot of top candidates from sparse scan |

---

## Next Steps (in priority order)

1. Try to get a clearer PAP-Link screenshot for 3/16 with readable PB episode times.
2. Scan unvisited ranges 0x058–0x249 and 0x30A–0x37F for PB/snoring candidates.
3. Try single-byte and bit-field interpretation across the full packet.
4. Implement computed PB detection in OSCAR from flow waveform data.
5. Find SpO2 and Pulse Rate offsets in the G3X 0x800-byte waveform packet.
