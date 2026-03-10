# TV Tuner GUI

Version `0.2.0`

Desktop Linux TV tuner application built with Qt 6. It scans channels with `w_scan2`, tunes live DVB/ATSC channels with `dvbv5-zap`, bridges playback through `ffmpeg`, keeps a cached TV Guide, supports scheduled channel changes, and includes app-wide display/theme controls.

## Main UI

The app currently has these top-level tabs:

- `Video`
- `TV Guide`
- `Meta Management`
- `Tuning`
- `Config`
- `Display Options`
- `Testing/bugs`
- `Logs`

## Features

### Video and Playback

- In-app live viewing with `dvbv5-zap` + `ffmpeg` + `QMediaPlayer`; there is no VLC handoff.
- Local file playback from the `Open File` button.
- Channel table built from scan results or saved channel data.
- Main `Video` page controls for `Watch Selected`, `Stop Watching`, `Open File`, `Pop Out Video`, `Fullscreen`, mute, and volume.
- Fullscreen playback window with its own overlay controls, playback status, signal status, and current-show info.
- Manual floating picture-in-picture from the main `Video` page.
- Optional automatic picture-in-picture when leaving the `Video` tab.
- Closing the floating PiP window returns video to the main player automatically; there is no separate `Return Video` button.
- Add/remove favorites directly from the watch view.
- Up to `10` quick favorite buttons for one-click channel changes.
- Current playback state, signal status, current show title, and synopsis shown in the main view.
- Optional frontend signal monitoring through `dvb-fe-tool --femon` when available.

### Live Playback Modes and Recovery

- `normal` mode passes the live transport stream through with `-c copy` for the lowest-latency, highest-fidelity path.
- `processed` mode deinterlaces video with `bwdif` and rebuilds audio/video before playback. This reduces interlacing combing and can sanitize ugly live transport issues, but it adds latency and disables raw passthrough.
- `resilient` mode is the first automatic recovery path after live playback failures. It rebuilds video and re-encodes audio to AAC stereo.
- `video-only` mode is a temporary fallback if rebuilt audio still fails during recovery.
- If rebuilt audio fails once, later rebuilt-audio retries stay muted until audio and video are both stable again.
- When `video-only` recovery survives visually, the app automatically retries an audio-capable path after a short stability window.
- Status-bar messages and logs show recovery transitions in real time.

### TV Guide

- Cached guide grid with `Guide`, `Search`, and `Status` sub-tabs.
- Guide timeline headers switch to `ddd HH:mm` formatting when the visible window spans multiple days.
- The visible guide window is capped at roughly `16` hours at a time, even if the cached JSON goes farther.
- Favorite-show scheduling can still use future data beyond the visible grid as long as it exists in the cached guide cache.
- Future airings can be scheduled directly from the guide.
- Search the cached guide by title or synopsis.
- Search results can be turned into favorite-show scheduling actions.
- Guide display can be filtered to hide channels without EIT data or show only favorites.
- `Reload Cache` requests a guide refresh and falls back to the current cache if refresh fails.
- Guide data can come from live OTA EIT capture or optional Schedules Direct OTA JSON downloads.
- The guide remains custom-rendered; theme changes feed its existing custom/HTML-style renderer instead of replacing it with standard app buttons.

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
- Playback options for automatic PiP-on-tab-change, processed live playback, and hiding the startup scheduled-switch summary.
- Background guide refresh interval and guide-cache retention settings.
- Optional Schedules Direct OTA JSON download and guide refresh source.
- Schedules Direct username, password hash, and ZIP/postal-code storage through app settings.
- The `Config` page is scrollable.
- The main `Config` sections are laid out in a cleaner two-column grid.
- Group boxes in the same `Config` row now size to the tallest box in that row for a more uniform layout.

### Display Options and Themes

- `Display Options` tab for editing the current display theme live.
- The current default theme matches the app’s present look:
  - black main background
  - red/black scrollbars
  - dark TV Guide look
- Named themes can be saved, loaded, overwritten, and deleted.
- The current working theme is persisted separately from named saved themes.
- Theme changes are loaded at startup before the main window is shown.
- Theme roles now cover the major visible UI surfaces, including:
  - app background and text
  - group borders
  - buttons
  - inputs
  - labels
  - tabs
  - menus
  - status bar
  - scrollbars
  - sliders
  - checkbox indicators
  - fullscreen overlay
  - TV Guide colors
  - TV Guide fonts
- Font controls include:
  - family
  - point size
  - bold
  - italic
  - underline
- Tab fonts are theme-driven.
- Slider colors are theme-driven.
- Checkbox indicator colors are theme-driven.

### Logging and Diagnostics

- Live scan, tuning, ffmpeg, player, guide, scheduling, recovery, and theme activity logs in the `Logs` tab.
- `Auto-scroll logs` checkbox so logs can keep appending while you inspect older entries in real time.
- TV Guide status text in the guide’s `Status` tab.
- `Testing/bugs` tab for saving ad hoc test/watch items.
- Status-bar updates for guide refreshes, playback state, cache activity, scheduling, PiP actions, theme activity, and recovery steps.

## Live Playback Pipeline

- Live tuner playback starts from device nodes such as `/dev/dvb/adapter0/dvr0`.
- `ffmpeg` bridges the live transport stream to a local UDP feed such as `udp://127.0.0.1:23000`.
- `QMediaPlayer` plays that local UDP stream inside the app.
- Processed and recovery modes run in memory through ffmpeg and player buffers; they do not intentionally create a temporary media file on disk.

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
- Optional: `dvb-fe-tool` in `PATH` for signal monitoring
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
- Display themes are stored in `display_themes.json` under the app-data location. If the app-data path cannot be resolved, the theme file falls back to the current working directory.
- Settings such as favorites, favorite-show rules, ratings, volume, mute state, PiP toggles, processed playback, guide/config options, and other lightweight app settings are stored with `QSettings`.
- The main log file defaults to `tv_tuner_gui.log` in the source tree. If the source tree path is unavailable, it falls back to the project working directory.
- You can override the log path with `TV_TUNER_GUI_LOG_PATH`.

## Notes

- The About dialog shows the compiled app version.
- The current build version is `0.2.0`.
- The app defaults to adapter `0`, frontend `0`, and country code `US`.
- On startup it forces `QT_QPA_PLATFORM=xcb`, `QT_XCB_GL_INTEGRATION=none`, `QT_MEDIA_BACKEND=ffmpeg`, and `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=none` when those variables are not already set.
- `processed` playback is meant for smoother motion and a cleaner live stream, not for true source-quality improvement.
- `normal` passthrough is still the best choice when you want the raw broadcast with minimum extra processing.
