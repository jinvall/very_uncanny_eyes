# Uncanny Eyes - Notes & Discussion

## Stream Configuration

### RTSP Server (Port 8554)
- **Mount point**: `/eyes`
- **Video**: H.264, 1280x720, 30fps
- **Bitrate**: 2000 kbps
- **Pipeline**: `ximagesrc` → `videoscale` → `x264enc` → `rtph264pay`

### Window Detection
The RTSP server auto-detects the uncanny eyes window using `xwininfo`. It captures only the window region, not the full desktop. This is critical for multi-monitor setups.

Known window: 1280x720 at position (320,1297) on dual-monitor setup.

### Dual-Monitor Display
- Display setup: 40-inch TV above 42-inch TV (stacked, joined X11 display)
- Window position varies: eyes window appears at (320,1297) on combined desktop
- The `videoscale` element in the pipeline forces exact 1280x720 output to satisfy H.264 even-dimension requirement

### Dependencies
```sh
sudo apt install -y gstreamer1.0-rtsp libgstrtspserver-1.0-0 \
  gir1.2-gst-rtsp-server-1.0 python3-gi
```

### Launch
```sh
./run_rtsp.sh
# or
DISPLAY=:0 python3 rtsp_server.py
```

### VLC URL
```
rtsp://10.0.0.147:8554/eyes
```

## Known Issues

1. **503 on DESCRIBE**: Pipeline fails if ximagesrc returns odd dimensions (e.g., 1281x721). Fix: `videoscale` forces 1280x720.
2. **Window detection fails**: If xwininfo doesn't find "uncanny" window, falls back to fullscreen (0,0 to 1920,1080).
3. **Port 8554 in use**: `pkill -f rtsp_server.py` then restart.
4. **App window not visible**: Ensure `DISPLAY=:0` and the app is running windowed (not fullscreen mode).
