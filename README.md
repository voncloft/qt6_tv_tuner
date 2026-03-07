# TV Tuner GUI (Qt6 + w_scan2)

Desktop GUI for scanning TV channels with `w_scan2` using Linux DVB devices.

## Features
- Scan controls for frontend type, country, adapter, and frontend.
- Uses explicit DVB path like `/dev/dvb/adapter0/frontend0`.
- Live scan logs in-app.
- Parsed channels table from scan output.
- In-app live viewer (no VLC handoff): select a channel and click `Watch Selected`.
- Favorites list with persistent storage.
- Six quick-switch favorite buttons for one-click channel changes.
- In-app audio controls (volume slider + mute toggle).
- Playback status indicator.
- Status bar policy: if something is happening, it should be shown there. Idle/default falls back to `Next JSON update ...`; transient status text is used for buffering, errors, failures, cache activity, guide-cache reloads, favorite auto-scheduling, current-show probes, and scheduled-switch processing.
- Favorite show priorities are edited in `Meta Management` with ratings from `1` (`don't care`) to `5` (`keep at all costs`).
- Show titles display their effective priority as `Title (rating: X)` in the main current/next area, the fullscreen overlay, and the TV Guide. Unrated shows display `rating: N/A`.
- `Config` includes a ratings override checkbox. When enabled, overlapping scheduled switches are resolved at runtime by the highest rating. Only ties at the top rating show a choice dialog, and that dialog shows each show's rating.
- TV Guide conflicts are not resolved up front anymore. Overlapping switches can stay queued, and the actual conflict is worked out only when the switch time arrives, either by ratings or by one runtime choice from you.
- Conflict choices can still lock a scheduled switch permanently by exact airing. If `Obey scheduled tuner switches` is disabled, queued switches stay saved but no runtime switch or conflict handling occurs.
- Fullscreen uses a temporary playback/status bar overlay with `Play`/`Pause`, volume, and mute. Moving the mouse or using input shows it, and after 10 seconds of inactivity both the overlay and the cursor hide again.
- Auto-reconnect attempts if the tuner/player stream drops.

## Build
```bash
cmake -S . -B build
cmake --build build -j
```

## Run
```bash
./build/tv_tuner_gui
```

## Notes
- Requires Qt6 Widgets and `w_scan2` in `PATH`.
- Requires `dvbv5-zap` in `PATH` for live watching.
- Defaults are set for your current setup: adapter `0`, frontend `0`, country `US`.
# qt6_tv_tuner
