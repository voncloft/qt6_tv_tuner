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
