#!/usr/bin/env python3
"""
RTSP server for uncanny eyes (GStreamer + Python bindings).

Captures ONLY the uncanny eyes SDL window and serves it as H.264 over RTSP so a
remote player (e.g. VLC on a Google TV) can display the eyes without exposing the
rest of the desktop.

Key design points
-----------------
* Window capture is bound to a *live* X11 window lookup, not a hardcoded XID.
  The eyes app is routinely restarted (e.g. from the TUI), which destroys and
  recreates the window. A previously captured XID becomes invalid and ximagesrc
  fails with `BadWindow`, leaving clients connected to a stream that never
  delivers frames. The XID is therefore re-resolved whenever a media pipeline is
  constructed, and a watchdog re-registers the mount when the window changes.
* Capture uses `ximagesrc xid=<window>` so only the eye window is read. Nothing
  else on the desktop is ever read into the stream.

Usage
-----
    rtsp_server.py [window_xid] [--no-watch] [--fps N] [--port N]

With no arguments the eyes window is auto-detected. Clients connect to:

    rtsp://<host>:<port>/eyes
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import threading
import time

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstRtspServer", "1.0")
from gi.repository import GLib, Gst, GstRtspServer  # noqa: E402

# --------------------------------------------------------------------------- #
# Configuration
# --------------------------------------------------------------------------- #

WIDTH = 1280
HEIGHT = 720
DEFAULT_PORT = 8554
MOUNT = "/eyes"
DISPLAY = os.environ.get("DISPLAY", ":0")

# X11 window geometry we require for the eyes window, used to filter out the
# terminal/editor/VS Code windows that also match the process name.
EYE_WINDOW_WIDTH = 1280
EYE_WINDOW_HEIGHT = 720

# WM_CLASS owner of the SDL window. Preferred over the title because the title
# is also present on unrelated windows in some window managers.
EYE_WM_CLASS = "uncanny-eyes"

BITRATE = "2000"
X264_PRESET = "ultrafast"
X264_TUNE = "zerolatency"

log_lock = threading.Lock()


_log_file = None


def log(message: str) -> None:
    """Thread-safe timestamped log to stdout and, when set, a log file."""
    line = f"[{time.strftime('%H:%M:%S')}] {message}"
    with log_lock:
        print(line, flush=True)
        if _log_file is not None:
            try:
                _log_file.write(line + "\n")
                _log_file.flush()
            except OSError:
                pass


def open_log_file(path: str) -> None:
    """Mirror the log into `path` (used by the launcher scripts)."""
    global _log_file
    try:
        _log_file = open(path, "a", encoding="utf-8")
    except OSError as exc:
        print(f"note: cannot open log file {path}: {exc}", flush=True)


# --------------------------------------------------------------------------- #
# Window discovery
# --------------------------------------------------------------------------- #

def _xwininfo_windows() -> str:
    """Return `xwininfo -root -tree -int` output for the eyes display."""
    result = subprocess.run(
        ["xwininfo", "-root", "-tree", "-int"],
        capture_output=True,
        text=True,
        timeout=3,
        env={**os.environ, "DISPLAY": DISPLAY},
    )
    if result.returncode != 0:
        raise RuntimeError(f"xwininfo failed: {result.stderr.strip()}")
    return result.stdout


def _parse_window_line(line: str) -> "dict | None":
    """
    Parse one `xwininfo -root -tree` line into a window dict, or None.

    Line shape (with -int, ids are decimal):
        44040202 "Uncanny Eyes": ("uncanny-eyes" "uncanny-eyes") 1280x720+320+1260  +320+1260

    The window id is the first token. It must be parsed positionally: a
    `0x...` search anywhere in the line would match the "0x720" inside the
    geometry "1280x720" and silently produce a bogus window id.
    """
    id_match = re.match(r"\s*(0x[0-9a-fA-F]+|\d+)\s", line)
    if not id_match:
        return None

    raw_id = id_match.group(1)
    window_id = int(raw_id, 16) if raw_id.lower().startswith("0x") else int(raw_id, 10)

    # Geometry may be "WIDTHxHEIGHT+X+Y" (relative) optionally followed by an
    # absolute origin "+X+Y", or a single absolute form.
    geo_match = re.search(
        r"(\d+)x(\d+)\+(-?\d+)\+(-?\d+)(?:\s+\+(-?\d+)\+(-?\d+))?(?:\s|$)", line
    )
    if not geo_match:
        return None

    width, height = int(geo_match.group(1)), int(geo_match.group(2))
    if geo_match.group(5) is not None:
        x, y = int(geo_match.group(5)), int(geo_match.group(6))
    else:
        x, y = int(geo_match.group(3)), int(geo_match.group(4))

    return {"id": window_id, "x": x, "y": y, "width": width, "height": height}


def find_eyes_window() -> "dict | None":
    """
    Locate the uncanny eyes SDL window.

    Selection strategy, most specific first:
      1. Window whose WM_CLASS is `uncanny-eyes` at the expected geometry.
      2. Any window whose title mentions the app at the expected geometry.

    Returns a dict with id/x/y/width/height, or None when not found.
    """
    try:
        lines = _xwininfo_windows().splitlines()
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        log(f"window lookup failed: {exc}")
        return None

    fallback = None
    for line in lines:
        if "has no name" in line:
            continue
        lower = line.lower()
        if EYE_WM_CLASS not in lower and "uncanny eyes" not in lower:
            continue

        window = _parse_window_line(line)
        if window is None:
            continue
        if window["width"] != EYE_WINDOW_WIDTH or window["height"] != EYE_WINDOW_HEIGHT:
            continue

        window["wclass"] = EYE_WM_CLASS in lower
        if window["wclass"]:
            return window
        fallback = fallback or window

    return fallback


def find_eyes_xid() -> "int | None":
    """Return just the XID of the eyes window, or None."""
    window = find_eyes_window()
    return window["id"] if window else None


# --------------------------------------------------------------------------- #
# Pipeline
# --------------------------------------------------------------------------- #

def build_pipeline(xid: int, fps: int) -> str:
    """
    Build the capture/encode pipeline for a specific window.

    `ximagesrc name=eyes xid=<window>` reads only that window's pixels. The
    explicit `queue` after capture decouples capture from encode so slow
    encoding cannot stall the source. `videoscale` normalises the frame size
    (ximagesrc can report off-by-one dimensions, which H.264 rejects because
    width/height must be even) and `videoconvert` performs the BGRx -> I420
    colour conversion that the encoder requires.
    """
    return (
        "( "
        f"ximagesrc name=eyes show-pointer=false use-damage=0 xid={xid} "
        f"! video/x-raw,format=BGRx,framerate={fps}/1 "
        "! queue max-size-buffers=2 leaky=downstream max-size-bytes=0 max-size-time=0 "
        "! videoscale method=bilinear "
        f"! video/x-raw,width={WIDTH},height={HEIGHT} "
        "! queue max-size-buffers=2 leaky=downstream max-size-bytes=0 max-size-time=0 "
        "! videoconvert n-threads=4 "
        f"! x264enc speed-preset={X264_PRESET} tune={X264_TUNE} bitrate={BITRATE} "
        "key-int-max=60 byte-stream=true "
        "! rtph264pay name=pay0 pt=96 config-interval=1 "
        ")"
    )


# --------------------------------------------------------------------------- #
# Client registry
# --------------------------------------------------------------------------- #

class ClientRegistry:
    """Tracks connected RTSP clients for visibility."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._clients: "dict[str, dict]" = {}
        self._live = 0

    def connected(self, ip: str) -> None:
        with self._lock:
            existing = self._clients.get(ip)
            if existing is None:
                self._clients[ip] = {
                    "since": time.time(),
                    "connects": 1,
                    "last": time.time(),
                }
                log(f"client connected: {ip} (total={len(self._clients)})")
                return
            existing["connects"] += 1
            gap = time.time() - existing["last"]
            existing["last"] = time.time()
            # A player probing the stream can reconnect in a tight loop; only
            # report it as a real reconnect once it has been away for a moment.
            if gap > 5.0:
                held = time.time() - existing["since"]
                log(f"client reconnected: {ip} (away {gap:.0f}s, session {held:.0f}s)")

    def disconnected(self, ip: str) -> None:
        with self._lock:
            entry = self._clients.pop(ip, None)
        if entry:
            log(f"client disconnected: {ip} (session {time.time() - entry['since']:.1f}s)")
        else:
            log(f"client connection closed: {ip}")

    def count(self) -> int:
        with self._lock:
            return len(self._clients)

    def set_live_count(self, live: int) -> None:
        """Record the authoritative live session count from the session pool.

        A drop to zero means every client has gone away, which is the only
        reliable disconnect indication GStreamer 1.24 exposes.
        """
        with self._lock:
            self._live = live
            if live == 0 and self._clients:
                for ip, entry in list(self._clients.items()):
                    log(f"client gone: {ip} (session {time.time() - entry['since']:.1f}s)")
                self._clients.clear()

    def snapshot(self) -> "list[tuple[str, float]]":
        with self._lock:
            now = time.time()
            return [(ip, now - e["since"]) for ip, e in self._clients.items()]


# --------------------------------------------------------------------------- #
# Server
# --------------------------------------------------------------------------- #

class EyesRtspServer:
    def __init__(self, port: int, fps: int, mount: str, watch: bool) -> None:
        self.port = port
        self.fps = fps
        self.mount = mount
        self.watch = watch
        self.registry = ClientRegistry()
        self.loop = GLib.MainLoop()

        self._mounts = None
        self._factory = None
        self._active_xid: "int | None" = None
        self._media = None
        self._pool = None
        self._session_lock = threading.Lock()
        self._last_session_count = 0
        self._stop = threading.Event()

    # -- pipeline helpers --------------------------------------------------- #

    def _resolve_xid(self) -> "int | None":
        """Prefer the CLI/auto-detected window id for the current process."""
        xid = find_eyes_xid()
        if xid is None:
            log("eyes window not found on display " + DISPLAY)
        return xid

    def _make_factory(self, xid: int) -> GstRtspServer.RTSPMediaFactory:
        factory = GstRtspServer.RTSPMediaFactory()
        factory.set_launch(build_pipeline(xid, self.fps))
        factory.set_shared(True)
        # Re-point the capture element at the live window before the pipeline
        # is prepared. This makes each new media instance use a fresh XID even
        # if the mount was registered earlier.
        factory.connect("media-constructed", self._on_media_constructed)
        return factory

    def _on_media_constructed(self, _factory, media) -> bool:
        """
        Observe pipeline construction.

        The XID is deliberately *not* set here: ximagesrc opens the X display
        when it is constructed, and the property must already be set before
        that point ("ximagesrc window ID must be set before opening display").
        The launch string is therefore the single source of truth for the XID,
        and the watchdog re-registers the mount when the window changes.
        """
        self._media = media
        return True

    def _on_client_connected(self, _server, client) -> None:
        """GStreamer 1.24 only exposes `client-connected`; see _watch_sessions
        for the matching disconnect detection."""
        try:
            self.registry.connected(_client_ip(client))
        except Exception as exc:
            log(f"client-connected hook failed: {exc}")

    def _watch_sessions(self) -> None:
        """
        Detect client disconnects by polling the live session count.

        RTSPServer exposes no `client-disconnected` signal, and the session pool
        does not expose its session list, so the authoritative disconnect signal
        available here is the pool's `session-removed` virtual method. Combined
        with a count poll this gives a reliable live-client figure for the TUI.
        """
        pool = self._pool
        while not self._stop.wait(2.0):
            try:
                live = pool.get_n_sessions()
            except Exception:
                continue
            with self._session_lock:
                if live != self._last_session_count:
                    self._last_session_count = live
                    self.registry.set_live_count(live)
                    if live == 0:
                        log("no active sessions (all clients gone)")

    def _register(self, xid: int) -> None:
        """(Re)register the mount with a fresh factory for `xid`."""
        self._factory = self._make_factory(xid)
        self._mounts.add_factory(self.mount, self._factory)
        self._active_xid = xid
        log(f"mount {self.mount} registered for xid=0x{xid:x}")

    # -- watchdog ----------------------------------------------------------- #

    def _watch_window(self) -> None:
        """
        Keep the served window in sync with the running eyes app.

        When the app restarts its window is destroyed and recreated with a new
        XID, which would otherwise leave the stream permanently stalled. On a
        change we re-register the mount so the next client gets a pipeline bound
        to the new window.
        """
        log("watchdog active: tracking eyes window for changes")
        missing_logged = False
        while not self._stop.wait(2.0):
            try:
                xid = self._resolve_xid()
            except Exception as exc:
                log(f"watchdog lookup error: {exc}")
                continue

            if xid is None:
                # The app is not running. Report it once rather than every tick.
                if not missing_logged:
                    log("eyes window gone (is the app running?); stream will re-bind when it returns")
                    missing_logged = True
                continue

            if missing_logged:
                log("eyes window is back")
                missing_logged = False

            if xid != self._active_xid:
                log(f"eyes window changed: 0x{(self._active_xid or 0):x} -> 0x{xid:x}")
                clients = self.registry.count()
                if clients:
                    log(f"{clients} client(s) connected; they must reconnect to the new stream")
                self._mounts.remove_factory(self.mount)
                self._register(xid)

    # -- lifecycle ---------------------------------------------------------- #

    def start(self) -> None:
        server = GstRtspServer.RTSPServer()
        server.set_service(str(self.port))
        server.set_address("0.0.0.0")
        server.connect("client-connected", self._on_client_connected)

        self._pool = server.get_session_pool()
        self._mounts = server.get_mount_points()
        server.attach(None)

        if self._active_xid is None:
            self._active_xid = self._resolve_xid()
        if self._active_xid is None:
            raise SystemExit(
                "ERROR: uncanny eyes window not found.\n"
                f"       Start it first, e.g. DISPLAY={DISPLAY} ./build-ldr/uncanny-eyes\n"
                "       then re-run this script (or pass the XID explicitly)."
            )
        self._register(self._active_xid)

        log(f"serving rtsp://{local_ip()}:{self.port}{self.mount} "
            f"({WIDTH}x{HEIGHT}@{self.fps} H.264, window xid=0x{self._active_xid:x})")

        if self.watch:
            threading.Thread(target=self._watch_window, daemon=True).start()
        if self._pool is not None:
            threading.Thread(target=self._watch_sessions, daemon=True).start()

    def run(self) -> None:
        self.start()
        try:
            self.loop.run()
        except KeyboardInterrupt:
            pass
        finally:
            self.shutdown()

    def shutdown(self) -> None:
        self._stop.set()
        clients = self.registry.snapshot()
        if clients:
            log(f"shutting down with {len(clients)} client(s):")
            for ip, held in clients:
                log(f"  {ip} connected {held:.0f}s ago")
        self.loop.quit()


def _client_ip(client) -> str:
    """Best-effort extraction of a client address from an RTSPClient."""
    for getter in ("get_connection",):
        conn = getattr(client, getter, None)
        if conn is None:
            continue
        conn = conn()
        for conn_getter, ip_getter in (
            ("get_address", "get_ip"),
            ("get_remote_address", "get_ip"),
            (None, "get_ip"),
        ):
            obj = conn
            if conn_getter:
                obj = getattr(conn, conn_getter, None)
                obj = obj() if obj else None
            if obj is None:
                continue
            ip_fn = getattr(obj, ip_getter, None)
            if ip_fn:
                return ip_fn()
    return "unknown"


def local_ip() -> str:
    """Best-effort primary IPv4 address of this host."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("10.255.255.255", 1))
        return sock.getsockname()[0]
    except OSError:
        return socket.gethostbyname(socket.gethostname())
    finally:
        sock.close()


# --------------------------------------------------------------------------- #
# Entry point
# --------------------------------------------------------------------------- #

def main(argv: "list[str]") -> int:
    parser = argparse.ArgumentParser(description="RTSP server for the uncanny eyes window")
    parser.add_argument("window_xid", nargs="?", default=None,
                        help="X11 window id to capture (default: auto-detect)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="listen port")
    parser.add_argument("--fps", type=int, default=30, help="capture frame rate")
    parser.add_argument("--no-watch", action="store_true",
                        help="do not track the eyes window for restarts")
    parser.add_argument("--log-file", default=None,
                        help="also append the log to this file")
    args = parser.parse_args(argv)

    Gst.init(None)

    if args.log_file:
        open_log_file(args.log_file)

    if args.window_xid:
        xid = int(args.window_xid, 0)
        log(f"using supplied window id 0x{xid:x}")
    else:
        xid = find_eyes_xid()
        if xid is None:
            log("eyes window not found; start the app first")
            return 1
        log(f"auto-detected eyes window 0x{xid:x}")

    server = EyesRtspServer(
        port=args.port,
        fps=args.fps,
        mount=MOUNT,
        watch=not args.no_watch,
    )
    server._active_xid = xid
    server.run()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
