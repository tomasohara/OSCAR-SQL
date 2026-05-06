"""Plot unknown offsets 0xC2 and 0xC8 from BMC legacy waveform packets.

Reads all waveform files (.000, .001, ...) for a given SD card image,
extracts the int16 values at the requested offsets (one per 256-byte packet),
and plots them for one OSCAR day (noon–noon) alongside neighbouring known
fields for context.

Usage:
    python plot_offsets.py [YYYY-MM-DD] [card_dir]

    card_dir  Full path or subdirectory name under Notes/Luna/.
              Defaults to "BMC Luna" if omitted.
    date      OSCAR day to plot. If omitted, lists available days and exits.
"""

import sys
import struct
import pathlib
import datetime

sys.stdout.reconfigure(encoding='utf-8')

try:
    import matplotlib
    matplotlib.use('Agg')  # non-interactive — saves to file without opening a window
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib not found — install with: pip install matplotlib")
    sys.exit(1)

# ---------- configuration ---------------------------------------------------

LUNA_ROOT = pathlib.Path(r"c:\OSCAR\OSCAR-code\Notes\Luna")
PACKET_SIZE = 256

# Offsets within each packet (byte offset → (label, struct_fmt))
# '<h' = signed 16-bit little-endian, '<H' = unsigned
FIELDS = {
    0x04: ("IPAP raw",          '<h'),
    0x06: ("EPAP raw",          '<h'),
    0xC2: ("Offset0xC2",        '<h'),
    0xC4: ("Leak",              '<h'),
    0xC6: ("TidalVolume",       '<h'),
    0xC8: ("Offset0xC8",        '<h'),
    0xCA: ("MinuteVentilation", '<h'),
}

# Highlighted offsets are drawn in red
HIGHLIGHT = [0xC2, 0xC8]

# ---------- helpers ----------------------------------------------------------

def iter_waveform_files(card_dir: pathlib.Path):
    """Return waveform file paths in order (.000, .001, ...)."""
    numeric = [f for f in card_dir.iterdir()
               if f.suffix.lstrip('.').isdigit()]
    return sorted(numeric, key=lambda p: int(p.suffix.lstrip('.')))


def read_packets(card_dir: pathlib.Path):
    """Return a list of dicts, one per packet, with decoded field values."""
    packets = []
    for fpath in iter_waveform_files(card_dir):
        data = fpath.read_bytes()
        n_packets = len(data) // PACKET_SIZE
        for i in range(n_packets):
            pkt = data[i * PACKET_SIZE : (i + 1) * PACKET_SIZE]
            if len(pkt) < PACKET_SIZE:
                break
            row = {}
            for offset, (label, fmt) in FIELDS.items():
                row[label] = struct.unpack_from(fmt, pkt, offset)[0]
            year   = struct.unpack_from('<H', pkt, 0xF8)[0]
            month  = pkt[0xFA]
            day    = pkt[0xFB]
            hour   = pkt[0xFC]
            minute = pkt[0xFD]
            second = pkt[0xFE]
            try:
                row['ts'] = datetime.datetime(year, month, day, hour, minute, second)
            except ValueError:
                row['ts'] = None
            packets.append(row)
    return packets


def oscar_day_key(ts: datetime.datetime) -> datetime.date:
    """Return the OSCAR day (starts at noon) that contains this timestamp."""
    if ts.hour < 12:
        return (ts - datetime.timedelta(days=1)).date()
    return ts.date()


def list_oscar_days(packets):
    """Print a summary of available OSCAR days."""
    from collections import Counter
    valid = [p for p in packets if p['ts'] is not None]
    counts = Counter(oscar_day_key(p['ts']) for p in valid)
    days = sorted(counts)
    print(f"\nAvailable OSCAR days ({len(days)} total):")
    for d in days:
        noon_start = datetime.datetime.combine(d, datetime.time(12, 0))
        noon_end   = noon_start + datetime.timedelta(days=1)
        day_packets = [p for p in valid
                       if noon_start <= p['ts'] < noon_end]
        print(f"  {d}  ({counts[d]} packets"
              f"  {noon_start.strftime('%H:%M')}–{noon_end.strftime('%H:%M')} "
              f"  first={day_packets[0]['ts'].strftime('%H:%M:%S') if day_packets else '?'}"
              f"  last={day_packets[-1]['ts'].strftime('%H:%M:%S') if day_packets else '?'})")
    print(f"\nUsage: python plot_offsets.py YYYY-MM-DD")


# ---------- main -------------------------------------------------------------

def resolve_card_dir(arg: str) -> pathlib.Path:
    p = pathlib.Path(arg)
    if p.is_absolute():
        return p
    return LUNA_ROOT / arg


def main():
    # Parse arguments: optional date and optional card dir
    target_date = None
    card_dir = LUNA_ROOT / "BMC Luna"

    for arg in sys.argv[1:]:
        try:
            target_date = datetime.date.fromisoformat(arg)
        except ValueError:
            card_dir = resolve_card_dir(arg)

    if not card_dir.is_dir():
        print(f"Card directory not found: {card_dir}")
        return

    packets = read_packets(card_dir)
    if not packets:
        print("No packets found — check card directory path.")
        return

    print(f"Loaded {len(packets)} packets from {card_dir.name}")

    if target_date is None:
        list_oscar_days(packets)
        return

    noon_start = datetime.datetime.combine(target_date, datetime.time(12, 0))
    noon_end   = noon_start + datetime.timedelta(days=1)

    day_packets = [p for p in packets
                   if p['ts'] is not None and noon_start <= p['ts'] < noon_end]

    if not day_packets:
        print(f"No packets found for OSCAR day {target_date}.")
        list_oscar_days(packets)
        return

    print(f"OSCAR day {target_date}: {len(day_packets)} packets  "
          f"({day_packets[0]['ts'].strftime('%H:%M:%S')} – "
          f"{day_packets[-1]['ts'].strftime('%H:%M:%S')})")

    timestamps = [p['ts'] for p in day_packets]

    # Print stats for target offsets
    for offset in HIGHLIGHT:
        label = FIELDS[offset][0]
        values = [p[label] for p in day_packets]
        print(f"\n{label} (0x{offset:02X}):")
        print(f"  min={min(values)}  max={max(values)}  "
              f"mean={sum(values)/len(values):.1f}  "
              f"first={values[0]}  last={values[-1]}")

    # Build figure: one subplot per field
    fig, axes = plt.subplots(len(FIELDS), 1, figsize=(14, 2.2 * len(FIELDS)),
                             sharex=True)
    fig.suptitle(f"BMC Legacy — OSCAR day {target_date}  "
                 f"({day_packets[0]['ts'].strftime('%H:%M')}–"
                 f"{day_packets[-1]['ts'].strftime('%H:%M')})\n{card_dir.name}",
                 fontsize=11)

    for ax, (offset, (label, _)) in zip(axes, sorted(FIELDS.items())):
        values = [p[label] for p in day_packets]
        color = 'tab:red' if offset in HIGHLIGHT else 'tab:blue'
        lw    = 1.4      if offset in HIGHLIGHT else 0.8
        ax.plot(timestamps, values, color=color, linewidth=lw)
        ax.set_ylabel(label, fontsize=8)
        ax.tick_params(axis='both', labelsize=7)
        ax.grid(True, alpha=0.3)
        if offset in HIGHLIGHT:
            ax.set_facecolor('#fff8f8')

    axes[-1].set_xlabel("Time")
    fig.tight_layout(rect=[0, 0, 1, 0.97])

    out = card_dir / f"offset_plots_{target_date}.png"
    fig.savefig(out, dpi=120)
    print(f"Plot saved to {out}")

    # --- correlation figure: 0xC2 vs Leak and IPAP/EPAP, shared x-axis ------
    corr_fields = [
        (0xC2, "Offset0xC2",  'tab:red'),
        (0xC4, "Leak",        'tab:green'),
        (0x04, "IPAP raw",    'tab:blue'),
        (0x06, "EPAP raw",    'tab:orange'),
    ]

    fig2, axes2 = plt.subplots(len(corr_fields), 1,
                               figsize=(14, 2.4 * len(corr_fields)),
                               sharex=True)
    fig2.suptitle(f"BMC Legacy — 0xC2 correlation  OSCAR day {target_date}\n"
                  f"({day_packets[0]['ts'].strftime('%H:%M')}–"
                  f"{day_packets[-1]['ts'].strftime('%H:%M')})",
                  fontsize=11)

    for ax, (offset, label, color) in zip(axes2, corr_fields):
        values = [p[label] for p in day_packets]
        ax.plot(timestamps, values, color=color, linewidth=0.9)
        ax.set_ylabel(label, fontsize=8)
        ax.tick_params(axis='both', labelsize=7)
        ax.grid(True, alpha=0.3)
        vmin, vmax = min(values), max(values)
        ax.set_title(f"min={vmin}  max={vmax}  mean={sum(values)/len(values):.1f}",
                     fontsize=7, loc='right', pad=2)

    axes2[-1].set_xlabel("Time")
    fig2.tight_layout(rect=[0, 0, 1, 0.97])

    out2 = card_dir / f"corr_c2_{target_date}.png"
    fig2.savefig(out2, dpi=120)
    print(f"Correlation plot saved to {out2}")


if __name__ == '__main__':
    main()
