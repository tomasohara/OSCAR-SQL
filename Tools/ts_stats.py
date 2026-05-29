#!/usr/bin/env python3
"""Report message statistics for Qt .ts translation files in a directory."""

import os
import sys


def analyse(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        content = f.read()
    msg = content.count("<message")
    unf = content.count('type="unfinished"')
    empty = content.count('type="unfinished"></translation>')
    obs = content.count('type="obsolete"')
    van = content.count('type="vanished"')
    return msg, unf, empty, obs, van


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    directory = os.path.abspath(directory)

    files = sorted(f for f in os.listdir(directory) if f.endswith(".ts"))
    if not files:
        print(f"No .ts files found in: {directory}")
        sys.exit(1)

    fmt = "{:<35} {:>8}  {:>10}  {:>7}  {:>8}  {:>8}  {:>8}"
    sep = "-" * 95

    print()
    print(f"Translation file statistics: {directory}")
    print(sep)
    print(fmt.format("File", "Messages", "Unfinished", "Empty", "Obsolete", "Vanished", "Active"))
    print(sep)

    tot_msg = tot_unf = tot_empty = tot_obs = tot_van = 0

    for fname in files:
        msg, unf, empty, obs, van = analyse(os.path.join(directory, fname))
        active = msg - obs - van
        tot_msg += msg
        tot_unf += unf
        tot_empty += empty
        tot_obs += obs
        tot_van += van
        name = os.path.splitext(fname)[0]
        print(fmt.format(name, msg, unf, empty, obs, van, active))

    print(sep)
    tot_active = tot_msg - tot_obs - tot_van
    print(fmt.format(f"TOTAL ({len(files)} files)", tot_msg, tot_unf, tot_empty, tot_obs, tot_van, tot_active))
    print()


if __name__ == "__main__":
    main()
