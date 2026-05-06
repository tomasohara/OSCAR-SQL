#!/usr/bin/env python3
# oscar_waveform_demo.py
#
# Demonstrator: read OSCAR's SQLite database and plot the Flow Rate and
# Pressure waveforms for the latest OSCAR day of a given profile.
#
# Spec: Notes/python_waveform_demo_spec.md
#
# Comments are intentionally verbose: this script's job is to teach readers
# how to talk to the OSCAR database, not to be production-terse.

import argparse
import sqlite3
import struct
import sys
import zlib
from datetime import datetime

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.dates as mdates


SUPPORTED_SCHEMA_VERSION = 13

EXIT_OK = 0
EXIT_USAGE = 1
EXIT_NOT_FOUND = 2
EXIT_SCHEMA = 3
EXIT_DECODE = 4

CHANNEL_CODES = ("FlowRate", "Pressure")


# --------------------------------------------------------------------------
# qCompress / qChecksum decoding helpers
# --------------------------------------------------------------------------

def qcompress_decode(payload: bytes) -> bytes:
    """Decode a Qt qCompress() byte array.

    Layout: first 4 bytes are a big-endian uint32 with the expected
    uncompressed length; the remainder is a standard zlib stream.
    """
    if len(payload) < 4:
        raise ValueError("qCompress payload too short")
    expected_len = struct.unpack(">I", payload[:4])[0]
    raw = zlib.decompress(payload[4:])
    if len(raw) != expected_len:
        raise ValueError(
            f"qCompress length mismatch: header says {expected_len}, "
            f"got {len(raw)} bytes after decompress"
        )
    return raw


def qchecksum(data: bytes) -> int:
    """Qt qChecksum (default Iso3309 / CRC-16/X-25).

    Polynomial 0x1021 reflected (0x8408), initial 0xFFFF, reflected in/out,
    final XOR 0xFFFF. Matches Qt's default qChecksum.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc ^ 0xFFFF


# --------------------------------------------------------------------------
# Step 1 — open the database
# --------------------------------------------------------------------------

def open_database(db_path: str) -> sqlite3.Connection:
    """Open oscar.db read-only."""
    uri = f"file:{db_path}?mode=ro"
    conn = sqlite3.connect(uri, uri=True)
    conn.row_factory = sqlite3.Row
    return conn


def check_schema_version(conn: sqlite3.Connection) -> None:
    row = conn.execute(
        "SELECT MAX(version) AS v FROM schema_version"
    ).fetchone()
    version = row["v"] if row else None
    if version != SUPPORTED_SCHEMA_VERSION:
        print(
            f"Schema version mismatch: database is v{version}, "
            f"this script supports v{SUPPORTED_SCHEMA_VERSION}.",
            file=sys.stderr,
        )
        sys.exit(EXIT_SCHEMA)


# --------------------------------------------------------------------------
# Step 2 — resolve profile name
# --------------------------------------------------------------------------

def resolve_profile(conn: sqlite3.Connection, username: str) -> int:
    row = conn.execute(
        "SELECT id FROM profiles WHERE username = ? AND status = 'active'",
        (username,),
    ).fetchone()
    if row is None:
        print(f"No active profile named {username!r}.", file=sys.stderr)
        sys.exit(EXIT_NOT_FOUND)
    return row["id"]


# --------------------------------------------------------------------------
# Step 3 — latest OSCAR day
# --------------------------------------------------------------------------

def latest_oscar_day(conn: sqlite3.Connection, profile_id: int) -> str:
    """Return the latest OSCAR day (YYYY-MM-DD) for this profile.

    OSCAR days run noon-to-noon. A session belongs to OSCAR day D if its
    start_time is in [D 12:00, D+1 12:00) local time — equivalently, shift
    start_time back 12 hours and take the calendar date.

    sessions.start_time is MILLISECONDS since the Unix epoch (not seconds),
    so we divide by 1000 before SQLite's date(..., 'unixepoch') sees it.
    """
    # Primary: daily_summaries is already keyed by OSCAR date.
    summary_row = conn.execute(
        "SELECT MAX(date) AS d FROM daily_summaries WHERE profile_id = ?",
        (profile_id,),
    ).fetchone()
    day_from_summary = summary_row["d"] if summary_row else None

    # Cross-check: compute the same value directly from sessions, so readers
    # see the OSCAR-day math.
    computed_row = conn.execute(
        """
        SELECT MAX(date(
            (s.start_time / 1000) - 12 * 3600,
            'unixepoch', 'localtime'
        )) AS d
        FROM sessions s
        JOIN machines m ON s.machine_id = m.id
        WHERE m.profile_id = ?
          AND s.enabled = 1
        """,
        (profile_id,),
    ).fetchone()
    day_from_sessions = computed_row["d"] if computed_row else None

    if day_from_summary is None and day_from_sessions is None:
        print("No sessions found for this profile.", file=sys.stderr)
        sys.exit(EXIT_NOT_FOUND)

    if (day_from_summary is not None
            and day_from_sessions is not None
            and day_from_summary != day_from_sessions):
        # Not fatal, but worth surfacing. Usually means daily_summaries is
        # stale (recomputed lazily by OSCAR).
        print(
            f"Warning: daily_summaries latest = {day_from_summary}, "
            f"sessions latest = {day_from_sessions}. Using sessions.",
            file=sys.stderr,
        )

    return day_from_sessions or day_from_summary


# --------------------------------------------------------------------------
# Step 4 — enumerate sessions on that day
# --------------------------------------------------------------------------

def sessions_on_day(
    conn: sqlite3.Connection, profile_id: int, oscar_day: str
) -> list[sqlite3.Row]:
    rows = conn.execute(
        """
        SELECT s.id         AS session_id,
               s.start_time AS start_time,
               s.end_time   AS end_time,
               m.brand      AS brand,
               m.model      AS model
        FROM sessions s
        JOIN machines m ON s.machine_id = m.id
        WHERE m.profile_id = ?
          AND date(
                (s.start_time / 1000) - 12 * 3600,
                'unixepoch', 'localtime'
              ) = ?
          AND s.enabled = 1
          AND s.summary_only = 0
        ORDER BY s.start_time
        """,
        (profile_id, oscar_day),
    ).fetchall()
    return rows


# --------------------------------------------------------------------------
# Step 5 — channel lookup
# --------------------------------------------------------------------------

def resolve_channels(
    conn: sqlite3.Connection, profile_id: int
) -> dict[str, dict]:
    """Return {channel_code: {channel_id, label, fullname, color}}.

    channels.channel_code is the stable identifier (e.g. 'CPAP_FlowRate');
    channels.channel_id is the integer used to join event_lists.
    """
    result: dict[str, dict] = {}
    for code in CHANNEL_CODES:
        row = conn.execute(
            """
            SELECT channel_id, label, fullname, default_color
            FROM channels
            WHERE profile_id = ? AND channel_code = ?
            """,
            (profile_id, code),
        ).fetchone()
        if row is None:
            print(
                f"Channel {code!r} is not defined for this profile.",
                file=sys.stderr,
            )
            sys.exit(EXIT_NOT_FOUND)
        result[code] = {
            "channel_id": row["channel_id"],
            "label": row["label"] or code,
            "fullname": row["fullname"] or code,
            "color": row["default_color"] or None,
        }
    return result


# --------------------------------------------------------------------------
# Steps 6–8 — load & decode an EventList (waveform or event type)
# --------------------------------------------------------------------------

def load_channel(
    conn: sqlite3.Connection, session_id: int, channel_id: int
) -> tuple[np.ndarray, np.ndarray, int]:
    """Return (times_ms, values, event_type) for one (session, channel).

    A single channel in one session may be split across multiple EventLists
    (one per contiguous run). Concatenate them in eventlist_index order.

    Returns event_type = 0 for waveforms (uniform sampling, rate ms apart)
    or 1 for events (non-uniform; each sample is a step change — plot with
    step-hold). 0 and 1 match the C++ `EventListType` enum.
    """
    event_lists = conn.execute(
        """
        SELECT id, first_time, last_time, count, rate, gain, offset,
               event_type, data_size, compressed_size
        FROM event_lists
        WHERE session_id = ? AND channel_id = ?
        ORDER BY eventlist_index
        """,
        (session_id, channel_id),
    ).fetchall()

    if not event_lists:
        return (
            np.empty(0, dtype=np.int64),
            np.empty(0, dtype=np.float32),
            0,
        )

    # All EventLists for one channel in one session share a type in practice.
    channel_event_type = int(event_lists[0]["event_type"])

    all_times: list[np.ndarray] = []
    all_values: list[np.ndarray] = []

    for el in event_lists:
        blob_row = conn.execute(
            """
            SELECT data_blob, data_compressed, compression_method, checksum
            FROM event_data WHERE eventlist_id = ?
            """,
            (el["id"],),
        ).fetchone()

        if blob_row is None:
            raise RuntimeError(
                f"event_data row missing for eventlist_id={el['id']}"
            )

        if blob_row["compression_method"] == 1:
            if blob_row["data_compressed"] is None:
                raise RuntimeError(
                    f"compression_method=1 but data_compressed is NULL "
                    f"(eventlist_id={el['id']})"
                )
            raw = qcompress_decode(bytes(blob_row["data_compressed"]))
        else:
            if blob_row["data_blob"] is None:
                raise RuntimeError(
                    f"compression_method=0 but data_blob is NULL "
                    f"(eventlist_id={el['id']})"
                )
            raw = bytes(blob_row["data_blob"])

        # Checksum: warn, don't abort — matches OSCAR's own behaviour.
        expected = blob_row["checksum"]
        if expected is not None:
            actual = qchecksum(raw)
            if actual != expected:
                print(
                    f"Warning: checksum mismatch on eventlist_id="
                    f"{el['id']} (expected {expected}, got {actual})",
                    file=sys.stderr,
                )

        count = el["count"]
        expected_bytes = count * 2  # int16
        if len(raw) < expected_bytes:
            raise RuntimeError(
                f"Primary data too short for eventlist_id={el['id']}: "
                f"count={count} needs {expected_bytes} bytes, got {len(raw)}"
            )

        # int16 little-endian, gain * raw + offset = physical value
        samples = np.frombuffer(raw, dtype="<i2", count=count)
        values = samples.astype(np.float32) * float(el["gain"]) + float(el["offset"])

        first_time = int(el["first_time"])
        el_type = int(el["event_type"])

        if el_type == 0:
            # Waveform: uniform spacing, first_time + i * rate (rate = ms/sample)
            rate_ms = float(el["rate"])
            times = first_time + (np.arange(count, dtype=np.float64) * rate_ms)
        else:
            # Event: time_blob holds uint32 ms-deltas from first_time, one per
            # sample. Load it separately and add to first_time for absolute ms.
            time_row = conn.execute(
                """
                SELECT time_blob, time_compressed
                FROM event_data WHERE eventlist_id = ?
                """,
                (el["id"],),
            ).fetchone()

            if time_row is None or (
                time_row["time_blob"] is None
                and time_row["time_compressed"] is None
            ):
                raise RuntimeError(
                    f"event EventList {el['id']} has no time array"
                )

            if time_row["time_compressed"] is not None:
                time_raw = qcompress_decode(bytes(time_row["time_compressed"]))
            else:
                time_raw = bytes(time_row["time_blob"])

            expected_time_bytes = count * 4  # uint32
            if len(time_raw) < expected_time_bytes:
                raise RuntimeError(
                    f"Time data too short for eventlist_id={el['id']}: "
                    f"count={count} needs {expected_time_bytes} bytes, "
                    f"got {len(time_raw)}"
                )
            deltas = np.frombuffer(time_raw, dtype="<u4", count=count)
            times = first_time + deltas.astype(np.int64)

        all_times.append(times.astype(np.int64))
        all_values.append(values)

    return (
        np.concatenate(all_times),
        np.concatenate(all_values),
        channel_event_type,
    )


# --------------------------------------------------------------------------
# Step 9 — plot
# --------------------------------------------------------------------------

def ms_to_mpl_dates(times_ms: np.ndarray) -> np.ndarray:
    """Convert ms-since-epoch to matplotlib date numbers (local time)."""
    # mpl.dates wants datetime objects or their float repr. Cheapest path:
    # build a vector of seconds, let mpl handle the rest via date2num.
    secs = times_ms.astype(np.float64) / 1000.0
    return mdates.date2num(
        np.array([datetime.fromtimestamp(s) for s in secs])
    )


def plot_day(
    oscar_day: str,
    sessions: list[sqlite3.Row],
    channels: dict[str, dict],
    per_session: list[dict],
) -> None:
    fig, (ax_flow, ax_press) = plt.subplots(
        2, 1, sharex=True, figsize=(12, 7)
    )

    flow_info = channels["FlowRate"]
    press_info = channels["Pressure"]

    for entry in per_session:
        label = f"{entry['brand']} {entry['model']}".strip() or "session"

        t, v, et = entry["FlowRate"]
        if t.size:
            # Flow is always a waveform — line plot.
            ax_flow.plot(ms_to_mpl_dates(t), v, linewidth=0.5, label=label)

        t, v, et = entry["Pressure"]
        if t.size:
            # Pressure is a step function for most loaders (event type);
            # use step-hold so each sample's value is carried forward.
            if et == 0:
                ax_press.plot(ms_to_mpl_dates(t), v, linewidth=0.8, label=label)
            else:
                ax_press.step(
                    ms_to_mpl_dates(t), v,
                    where="post", linewidth=1.0, label=label,
                )

    # Session boundaries: dashed verticals at start_time of each session.
    for s in sessions:
        x = ms_to_mpl_dates(np.array([s["start_time"]], dtype=np.int64))[0]
        for ax in (ax_flow, ax_press):
            ax.axvline(x, color="0.7", linestyle="--", linewidth=0.5)

    ax_flow.set_ylabel(f"{flow_info['label']} (L/min)")
    ax_press.set_ylabel(f"{press_info['label']} (cmH₂O)")
    ax_press.set_xlabel(f"Time — OSCAR day {oscar_day}")

    ax_press.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    fig.autofmt_xdate()

    # Deduplicate legend entries (one machine may span many sessions).
    handles, labels = ax_flow.get_legend_handles_labels()
    seen: dict[str, object] = {}
    for h, l in zip(handles, labels):
        seen.setdefault(l, h)
    if seen:
        ax_flow.legend(list(seen.values()), list(seen.keys()), loc="upper right")

    fig.suptitle(f"OSCAR waveforms — {oscar_day}")
    fig.tight_layout()
    plt.show()


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot Flow Rate and Pressure for a profile's latest OSCAR day."
    )
    parser.add_argument("db_path", help="Path to OSCAR's oscar.db file")
    parser.add_argument("profile_name", help="profiles.username value")
    args = parser.parse_args()

    conn = open_database(args.db_path)
    try:
        check_schema_version(conn)

        profile_id = resolve_profile(conn, args.profile_name)
        print(f"Profile {args.profile_name!r} → profile_id={profile_id}")

        oscar_day = latest_oscar_day(conn, profile_id)
        print(f"Latest OSCAR day: {oscar_day}")

        sessions = sessions_on_day(conn, profile_id, oscar_day)
        if not sessions:
            print("No plottable sessions on that day.", file=sys.stderr)
            return EXIT_NOT_FOUND
        total_ms = sum(s["end_time"] - s["start_time"] for s in sessions)
        print(
            f"Found {len(sessions)} session(s), "
            f"total {total_ms / 60000:.1f} minutes."
        )

        channels = resolve_channels(conn, profile_id)
        for code, info in channels.items():
            print(
                f"  {code:20s} channel_id={info['channel_id']:6d}  "
                f"label={info['label']!r}"
            )

        per_session = []
        try:
            for s in sessions:
                entry = {
                    "session_id": s["session_id"],
                    "brand": s["brand"] or "",
                    "model": s["model"] or "",
                }
                for code, info in channels.items():
                    entry[code] = load_channel(
                        conn, s["session_id"], info["channel_id"]
                    )
                per_session.append(entry)
        except (RuntimeError, ValueError) as exc:
            print(f"Blob decode failed: {exc}", file=sys.stderr)
            return EXIT_DECODE

        plot_day(oscar_day, sessions, channels, per_session)
        return EXIT_OK
    finally:
        conn.close()


if __name__ == "__main__":
    sys.exit(main())
