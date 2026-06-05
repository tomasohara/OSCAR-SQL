#!/usr/bin/env python3
"""Report message statistics for Qt .ts translation files in a directory."""

import os
import sys
import xml.etree.ElementTree as ET


def analyse(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        content = f.read()
    msg = content.count("<message")
    unf = content.count('type="unfinished"')
    empty = content.count('type="unfinished"></translation>') + content.count('type="unfinished" />')
    obs = content.count('type="obsolete"')
    van = content.count('type="vanished"')

    same = 0
    try:
        tree = ET.parse(path)
        for m in tree.iter("message"):
            t = m.find("translation")
            if t is None:
                continue
            if t.get("type") in ("obsolete", "vanished"):
                continue
            src = m.find("source")
            if src is None:
                continue
            t_text = t.text or ""
            s_text = src.text or ""
            if t_text and t_text == s_text:
                same += 1
    except ET.ParseError:
        pass

    return msg, unf, empty, obs, van, same


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    directory = os.path.abspath(directory)

    files = sorted(f for f in os.listdir(directory) if f.endswith(".ts"))
    if not files:
        print(f"No .ts files found in: {directory}")
        sys.exit(1)

    names = [os.path.splitext(f)[0] for f in files]
    total_label = f"TOTAL ({len(files)} files)"
    col_w = max(len(n) for n in names + [total_label]) + 2
    fmt = f"{{:<{col_w}}} {{:>8}}  {{:>10}}  {{:>7}}  {{:>8}}  {{:>8}}  {{:>8}}  {{:>10}}"
    sep = "-" * (col_w + 72)

    print()
    print(f"Translation file statistics: {directory}")
    print(sep)
    print(fmt.format("File", "Messages", "Unfinished", "Empty", "Obsolete", "Vanished", "Active", "SameAsSrc"))
    print(sep)

    tot_msg = tot_unf = tot_empty = tot_obs = tot_van = tot_same = 0

    for fname in files:
        msg, unf, empty, obs, van, same = analyse(os.path.join(directory, fname))
        active = msg - obs - van
        tot_msg += msg
        tot_unf += unf
        tot_empty += empty
        tot_obs += obs
        tot_van += van
        tot_same += same
        name = names[files.index(fname)]
        print(fmt.format(name, msg, unf, empty, obs, van, active, same))

    print(sep)
    tot_active = tot_msg - tot_obs - tot_van
    print(fmt.format(total_label, tot_msg, tot_unf, tot_empty, tot_obs, tot_van, tot_active, tot_same))
    print()


if __name__ == "__main__":
    main()
