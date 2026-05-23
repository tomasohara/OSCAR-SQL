● **The type field is mostly a label, but with two functional exceptions:**

Purely cosmetic (no effect on correction math):
  - travel, dst, reset — these are processed identically to offset in correctionMs(). All non-drift types go through the same branch: total += row.offsetMs. History table row coloring is the only difference.

  Functionally distinct:

1. "timezone" type triggers two real behaviors:

    - effectiveDateRange() uses "" (NULL in DB) for open-ended instead of "2099-12-31" used by all other types
    - On Save, closeOpenTimezoneRows() automatically closes any prior open-ended timezone rows for the same device — so
a new timezone correction supersedes the old one rather than stacking
  2. "offset" type is specifically queried by findManualOffsetRows() when the drift analysis dialog loads
    reference-device measurement points. Only rows of type "offset" are treated as nightly clock-delta measurements for the drift fit; travel/dst entries are excluded from that query.

So practically: travel, dst, and reset are organizational labels to help the user understand why a correction was applied — the math treats them identically to offset. The timezone type has real behavioral consequences (auto-closing superseded rows, NULL-based open-ended), and offset is the only type the drift fitting pipeline reads.