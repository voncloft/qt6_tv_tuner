# TV Tuner GUI

Desktop Linux TV tuner application built with Qt 6. It scans channels with `w_scan2`, plays live DVB/ATSC channels inside the app, keeps a cached TV Guide, and lets you schedule channel switches from guide data.

## Main UI

The app currently has these top-level tabs:

- `Video`
- `TV Guide`
- `Meta Management`
- `Tuning`
- `Config`
- `Testing/bugs`
- `Logs`

## Features

### Video

- In-app live viewing with `dvbv5-zap` + `ffmpeg`; no VLC handoff.
- Local file playback from the `Open File` button.
- Channel table built from scan results or saved channel data.
- `Watch Selected`, `Stop Watching`, `Fullscreen`, mute, and volume controls.
- Fullscreen playback window with its own overlay controls and current-show info.
- Optional mini-player / picture-in-picture window when leaving the `Video` tab.
- Add/remove favorites directly from the watch view.
- Up to `10` quick favorite buttons for one-click channel changes.
- Current playback state, current show title, and synopsis shown in the main view.

### TV Guide

- Cached guide grid with a channel column and time-based timeline.
- `Guide`, `Search`, and `Status` sub-tabs inside the TV Guide view.
- Future airings can be scheduled directly from the guide.
- Search the cached guide by title or synopsis.
- Search results can be turned into favorite-show switches with `Add Favorite Switch`.
- Guide display can be filtered to hide channels without EIT data or show only favorites.
- `Reload Cache` requests a guide refresh and falls back to the current cache if refresh fails.

### Scheduling and Favorites

- Favorite channels are persisted and shown as quick-switch buttons.
- Favorite show rules are managed in `Meta Management`.
- Favorite show priority ratings run from `1` (`don't care`) to `5` (`keep at all costs`).
- Auto-scheduling can queue future matches for favorite shows from cached guide data.
- Runtime scheduling conflicts can be resolved by favorite-show priority ratings.
- Overlapping scheduled switches can remain queued until airtime, then be resolved at runtime.
- Scheduled switches are persisted and managed from the `Scheduled Switches` list.

### Config and Data Sources

- Scan settings for frontend type, country code, adapter, frontend, and output format.
- Background guide refresh interval and guide-cache retention settings.
- Optional Schedules Direct OTA JSON download and guide refresh source.
- Option to hide the scheduled-switch startup summary.
- Option to pop video out automatically when leaving the `Video` tab.

### Logging and Miscellaneous

- Live scan/tuning/playback logs in the `Logs` tab.
- TV Guide status text in the guide's `Status` tab.
- A `Testing/bugs` tab for saving ad hoc test/watch items.
- Status bar updates for background guide refreshes, playback state, cache activity, and scheduling activity.

## Build Requirements

- CMake 3.16+
- C++17 compiler
- Qt 6 modules:
  - `Widgets`
  - `Multimedia`
  - `MultimediaWidgets`
  - `Network`

## Runtime Requirements

- Linux DVB device nodes such as `/dev/dvb/adapter0/frontend0`
- `w_scan2` in `PATH` for channel scans
- `dvbv5-zap` in `PATH` for live tuning
- `ffmpeg` in `PATH` for the live playback bridge
- `timeout` and `dd` in `PATH` for live EIT guide capture
- Optional: Schedules Direct credentials plus ZIP/postal code for OTA JSON downloads

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/tv_tuner_gui
```

## Stored Data

- `channels.conf`, guide cache JSON, scheduled-switch JSON, Schedules Direct export JSON, and channel hints are stored under the app-data location returned by `QStandardPaths::AppDataLocation`.
- Settings such as favorites, favorite-show rules, ratings, and toggles are stored with `QSettings`.
- The main log file is `tv_tuner_gui.log` in the source/project directory by default.
- You can override the log path with `TV_TUNER_GUI_LOG_PATH`.

## Notes

- The app defaults to adapter `0`, frontend `0`, and country code `US`.
- On startup it forces `QT_QPA_PLATFORM=xcb`, `QT_XCB_GL_INTEGRATION=none`, `QT_MEDIA_BACKEND=ffmpeg`, and software video decode when those variables are not already set.
