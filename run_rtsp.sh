#!/bin/bash
# Launch the uncanny eyes app plus its RTSP server, streaming ONLY the eye window.
#
# Clients connect to: rtsp://<host>:8554/eyes
#
# Prerequisites:
#   sudo apt install -y gstreamer1.0-rtsp libgstrtspserver-1.0-0 \
#     gir1.2-gst-rtsp-server-1.0 python3-gi

set -euo pipefail

WIDTH=1280
HEIGHT=720
PORT=8554
DISPLAY_NUM="${DISPLAY_NUM:-0}"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Uncanny Eyes RTSP Stream ==="
echo "Streaming ONLY the eye window (never the desktop)"

# Stop any previous instance so the port and window are free.
pkill -f "uncanny-eyes" 2>/dev/null || true
pkill -f "rtsp_server.py" 2>/dev/null || true
sleep 1

# Borderless keeps the captured frame free of title bars and window chrome,
# and --background 0,0,0 keeps it pure black.
cd "$PROJECT_DIR"
DISPLAY=:"$DISPLAY_NUM" ./build-ldr/uncanny-eyes \
    --width "$WIDTH" --height "$HEIGHT" --borderless --background 0,0,0 &
APP_PID=$!
echo "Eyes app started (PID $APP_PID)"

# Wait for the SDL window to map, then confirm it exists. Window creation can
# take longer than a fixed sleep allows, so poll until it appears or time out.
# The XID is deliberately NOT parsed here: it is resolved inside rtsp_server.py,
# which also watches the window and re-binds when the app restarts. Grepping an
# id out of xwininfo output is unsafe because the geometry "1280x720" itself
# contains a "0x720" substring.
window_up() {
    # Capture first, then match. Piping straight into `grep -q` under
    # `set -o pipefail` fails: grep exits on the first match, xwininfo takes
    # SIGPIPE (141), and the whole pipeline is reported as an error.
    local tree
    tree=$(DISPLAY=:"$DISPLAY_NUM" xwininfo -root -tree -int 2>/dev/null) || return 1
    case $tree in
        *'"Uncanny Eyes": ("uncanny-eyes"'*) return 0 ;;
        *) return 1 ;;
    esac
}

for _ in $(seq 1 20); do
    if window_up; then
        break
    fi
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        echo "ERROR: eyes app exited during startup" >&2
        exit 1
    fi
    sleep 0.5
done

if ! window_up; then
    echo "ERROR: uncanny eyes window not found on display :$DISPLAY_NUM" >&2
    kill "$APP_PID" 2>/dev/null || true
    exit 1
fi
echo "Eyes window detected"

# Start the RTSP server. It auto-detects the window and watches for restarts.
# The server writes its own log file (rather than piping through tee) so the
# console output is not duplicated.
rm -f /tmp/rtsp_server.log
DISPLAY=:"$DISPLAY_NUM" /usr/bin/python3 -u "$PROJECT_DIR/rtsp_server.py" \
    --port "$PORT" --log-file /tmp/rtsp_server.log &
RTSP_PID=$!
echo "RTSP server started (PID $RTSP_PID)"

sleep 2

if ss -tln 2>/dev/null | grep -q ":$PORT"; then
    echo "RTSP server listening on port $PORT"
else
    echo "WARNING: RTSP server may not have bound port $PORT" >&2
    tail -n 15 /tmp/rtsp_server.log >&2 || true
fi

LOCAL_IP=$(hostname -I | awk '{print $1}')
echo
echo "=== Stream ready ==="
echo "VLC: rtsp://$LOCAL_IP:$PORT/eyes"
echo "Press Ctrl+C to stop."
echo

cleanup() {
    trap - EXIT INT TERM
    kill "$RTSP_PID" "$APP_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Supervise rather than `wait` on both PIDs. The eyes app is expected to come and
# go (the TUI restarts it, designs change, etc.) and the whole point of the
# server's window watchdog is to survive that. Exiting when the app died would
# tear the RTSP server down with it, which is exactly the failure where clients
# see a dead stream after an app restart.
while true; do
    if ! kill -0 "$RTSP_PID" 2>/dev/null; then
        echo "RTSP server exited; stopping" >&2
        break
    fi
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        echo "eyes app exited; restarting it (stream stays up)"
        cd "$PROJECT_DIR"
        DISPLAY=:"$DISPLAY_NUM" ./build-ldr/uncanny-eyes \
            --width "$WIDTH" --height "$HEIGHT" --borderless --background 0,0,0 &
        APP_PID=$!
    fi
    sleep 2
done
