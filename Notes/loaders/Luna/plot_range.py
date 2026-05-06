"""Plot every int16 word in a byte range for one OSCAR day.

Usage:
    python plot_range.py YYYY-MM-DD [card_dir]

    card_dir defaults to "BMC Luna".
    Plots each word as its own subplot, all sharing the same time axis.
    Output saved as <card_dir>/range_plot_<start>-<end>_<date>.png
"""

import sys
import struct
import pathlib
import datetime

sys.stdout.reconfigure(encoding='utf-8')

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib not found — install with: pip install matplotlib")
    sys.exit(1)

# ---------- configuration ---------------------------------------------------

LUNA_ROOT   = pathlib.Path(r"c:\OSCAR\OSCAR-code\Notes\Luna")
PACKET_SIZE = 256

# Byte range to plot (inclusive of both ends; rounded down to word boundaries)
RANGE_START = 0x9E
RANGE_END   = 0xBD   # last word starting at 0xBC covers 0xBC-0xBD

# Build list of (offset, label) for every int16 word in range
WORDS = [(off, f"0x{off:02X}") for off in range(RANGE_START, RANGE_END, 2)]

# ---------- helpers ----------------------------------------------------------

def iter_waveform_files(card_dir):
    numeric = [f for f in card_dir.iterdir() if f.suffix.lstrip('.').isdigit()]
    return sorted(numeric, key=lambda p: int(p.suffix.lstrip('.')))


def read_packets(card_dir):
    packets = []
    for fpath in iter_waveform_files(card_dir):
        data = fpath.read_bytes()
        n = len(data) // PACKET_SIZE
        for i in range(n):
            base = i * PACKET_SIZE
            pkt  = data[base : base + PACKET_SIZE]
            if len(pkt) < PACKET_SIZE:
                break
            row = {}
            for off, label in WORDS:
                row[label] = struct.unpack_from('<h', pkt, off)[0]
            year   = struct.unpack_from('<H', pkt, 0xF8)[0]
            month  = pkt[0xFA]; day = pkt[0xFB]
            hour   = pkt[0xFC]; minute = pkt[0xFD]; second = pkt[0xFE]
            try:
                row['ts'] = datetime.datetime(year, month, day, hour, minute, second)
            except ValueError:
                row['ts'] = None
            packets.append(row)
    return packets


def oscar_day_bounds(date):
    noon = datetime.datetime.combine(date, datetime.time(12, 0))
    return noon, noon + datetime.timedelta(days=1)

# ---------- main -------------------------------------------------------------

def main():
    target_date = None
    card_dir    = LUNA_ROOT / "BMC Luna"

    for arg in sys.argv[1:]:
        try:
            target_date = datetime.date.fromisoformat(arg)
        except ValueError:
            p = pathlib.Path(arg)
            card_dir = p if p.is_absolute() else LUNA_ROOT / arg

    if not card_dir.is_dir():
        print(f"Card directory not found: {card_dir}")
        return
    if target_date is None:
        print("Usage: python plot_range.py YYYY-MM-DD [card_dir]")
        return

    print(f"Reading packets from {card_dir.name} ...")
    packets = read_packets(card_dir)
    print(f"Loaded {len(packets)} packets")

    noon_start, noon_end = oscar_day_bounds(target_date)
    day = [p for p in packets if p['ts'] is not None and noon_start <= p['ts'] < noon_end]

    if not day:
        print(f"No packets for OSCAR day {target_date}")
        return

    print(f"OSCAR day {target_date}: {len(day)} packets  "
          f"({day[0]['ts'].strftime('%H:%M:%S')} – {day[-1]['ts'].strftime('%H:%M:%S')})")

    timestamps = [p['ts'] for p in day]

    n = len(WORDS)
    fig, axes = plt.subplots(n, 1, figsize=(14, 1.9 * n), sharex=True)
    fig.suptitle(
        f"BMC Legacy — 0x{RANGE_START:02X}–0x{RANGE_END:02X} (1 Hz scalars)  "
        f"OSCAR day {target_date}\n"
        f"{card_dir.name}  "
        f"({day[0]['ts'].strftime('%H:%M')}–{day[-1]['ts'].strftime('%H:%M')})",
        fontsize=10)

    for ax, (off, label) in zip(axes, WORDS):
        values = [p[label] for p in day]
        ax.plot(timestamps, values, linewidth=0.8, color='tab:blue')
        vmin, vmax = min(values), max(values)
        mean = sum(values) / len(values)
        ax.set_ylabel(label, fontsize=8)
        ax.tick_params(axis='both', labelsize=7)
        ax.grid(True, alpha=0.3)
        ax.set_title(f"min={vmin}  max={vmax}  mean={mean:.1f}",
                     fontsize=7, loc='right', pad=2)

    axes[-1].set_xlabel("Time")
    fig.tight_layout(rect=[0, 0, 1, 0.97])

    out = card_dir / f"range_plot_{RANGE_START:02X}-{RANGE_END:02X}_{target_date}.png"
    fig.savefig(out, dpi=120)
    print(f"Plot saved to {out}")


if __name__ == '__main__':
    main()
