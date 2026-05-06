# SHG File Use

## What they are

`.shg` = "SleepyHead Graph" settings files (the extension dates from the original SleepyHead project).
They are binary `QDataStream` files storing the user's customized layout for each graph view.

Files: `Profiles/<profile name>/daily.shg` and `Profiles/<profile name>/overview.shg`

## What each file stores (per graph panel)

- Height of the panel
- Visible/hidden state
- Y-axis min/max (`RecMinY`, `RecMaxY`)
- Y-zoom mode
- Pinned status
- For line chart layers: per-channel enabled state, flags, dot enabled state

## daily.shg — Daily view layout

- Loaded: `Daily` constructor (`daily.cpp:579`)
- Saved: `Daily` destructor (`daily.cpp:612`), and from `PreferencesDialog` (`preferencesdialog.cpp:1138`)

## overview.shg — Overview view layout

- Loaded: `Overview` constructor (`overview.cpp:177`), and on profile switch (`overview.cpp:395`)
- Saved: `Overview` destructor (`overview.cpp:202`), on profile switch (`overview.cpp:388`), and from `PreferencesDialog` (`preferencesdialog.cpp:1139`)

## Status

Actively used. These are the sole mechanism for persisting user-customized graph layouts across
sessions. Without them, every OSCAR startup resets all graph panels to default heights, order,
visibility, and Y-axis ranges. There is no equivalent in the database.
