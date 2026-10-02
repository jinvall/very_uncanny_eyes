#!/usr/bin/env python3
"""Uncanny Eyes TUI Controller.

Design selection is index-based: the variant list is discovered from the binary
at startup via `--list-designs`, so it always matches the compiled registry.
Adding a variant means editing the C++ registry only -- never this file.
Restart the TUI after a rebuild to pick up new variants.
"""
import glob, os, subprocess, sys, threading, time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent
BINARY = PROJECT_DIR / "build-ldr" / "uncanny-eyes"
MODES = ["normal","dozing"]

# Single-character commands. Anything else typed at the prompt is treated as a
# design selection (2-digit index or design name).
COMMANDS = set("mflt") | set("srxq")

def log(msg):
    print(f"[TUI] {msg}", file=sys.stderr, flush=True)

def _local_ip():
    """Primary IPv4 address, for printing the stream URL."""
    import socket as s
    sock = s.socket(s.AF_INET, s.SOCK_DGRAM)
    try:
        sock.connect(("10.255.255.255", 1))
        return sock.getsockname()[0]
    except OSError:
        return s.gethostbyname(s.gethostname())
    finally:
        sock.close()

def _stream_port_open(port=8554):
    """True when something is listening on the RTSP port."""
    import socket as s
    sock = s.socket()
    sock.settimeout(0.3)
    try:
        return sock.connect_ex(("127.0.0.1", port)) == 0
    finally:
        sock.close()

def _drain(proc, sink=None):
    """Read a child's output line by line so its log is never buffered."""
    if not proc or not proc.stdout:
        return
    for raw in proc.stdout:
        line = raw.decode(errors="replace").rstrip()
        if sink is not None:
            sink.append(line)
            del sink[:-40]
        else:
            log(line)

def find_ldr_port():
    """Auto-detect the LDR serial port."""
    for pattern in ["/dev/ttyUSB*", "/dev/ttyACM*"]:
        ports = sorted(glob.glob(pattern))
        for p in ports:
            if os.path.exists(p):
                return p
    return None


def load_designs():
    """Ask the binary which designs it has.

    The list is whatever artwork_registry.cpp contains, so it cannot drift out
    of sync with the build the way a hardcoded list does.
    """
    try:
        out = subprocess.run(
            [str(BINARY), "--list-designs"],
            capture_output=True, text=True, timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as e:
        log(f"could not run {BINARY} --list-designs: {e}")
        return []
    if out.returncode != 0:
        log(f"--list-designs exited {out.returncode}: {out.stderr.strip()}")
        return []
    return [line.strip() for line in out.stdout.splitlines() if line.strip()]


def design_label(index):
    """Two-digit selector for a design index, e.g. 0 -> '01'."""
    return f"{index + 1:02d}"


def design_range(designs):
    """Selectable index range as '01-06', or '(none)' when empty."""
    return f"01-{design_label(len(designs) - 1)}" if designs else "(none)"


def resolve_design(raw, designs):
    """Resolve typed text to a design index.

    Accepts a 1-2 digit index ('07', '7') or a design name / unique prefix.
    Returns (index, error); index is None when the input did not resolve.
    """
    text = raw.strip().lower()
    if not text:
        return None, None
    if text.isdigit():
        idx = int(text) - 1
        if 0 <= idx < len(designs):
            return idx, None
        return None, f"no design {text} (have {design_range(designs)})"
    matches = [i for i, name in enumerate(designs) if name.lower().startswith(text)]
    if len(matches) == 1:
        return matches[0], None
    if not matches:
        return None, f"no design matching {text!r}"
    return None, "ambiguous: " + ", ".join(designs[i] for i in matches)

class PM:
    def __init__(self):
        self.proc = None
        self.out = []
        self.pid = None

    def start(self, args):
        self.stop()
        env = dict(os.environ)
        try:
            self.proc = subprocess.Popen(
                [str(BINARY)] + args,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL, env=env
            )
            self.pid = self.proc.pid
            self.out = [f"PID {self.pid}"]
            t = __import__("threading").Thread(target=self._read, daemon=True)
            t.start()
            return True, f"Started (PID {self.pid})"
        except Exception as e:
            return False, str(e)

    def stop(self):
        if self.proc:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=3)
            except:
                self.proc.kill()
            self.proc = None
            self.pid = None
        log("Stopped")

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def _read(self):
        if not self.proc or not self.proc.stdout:
            return
        for line in self.proc.stdout:
            self.out.append(line.decode(errors="replace").rstrip())
            self.out = self.out[-20:]


def main():
    log("=== TUI ===")
    pm = PM()
    designs = load_designs()
    if not designs:
        log(f"ERROR: no designs found. Is {BINARY} built?")
    else:
        log(f"{len(designs)} designs: " + ", ".join(
            f"{design_label(i)}={n}" for i, n in enumerate(designs)))
    current = 0
    mode = 0
    fullscreen = False
    ldr_on = False
    ldr_port = find_ldr_port() or "/dev/ttyUSB0"
    stream_proc = None
    stream_out = []

    while True:
        os.system("clear")
        print("=" * 50)
        print("  UNCANNY EYES CONTROLLER")
        print("=" * 50)
        current_name = designs[current] if designs else "(none)"
        print(f"  Design:     {design_label(current)} {current_name}")
        print(f"  Mode:       {MODES[mode]}")
        print(f"  Fullscreen: {'Yes' if fullscreen else 'No'}")
        print(f"  LDR:        {'ON' if ldr_on else 'OFF'} ({ldr_port})")
        print(f"  Process:    {'▶ RUNNING' if pm.running() else '■ STOPPED'}")

        # Reflect reality: the stream may have been started outside this TUI
        # (e.g. by run_rtsp.sh), so the listening port is the source of truth.
        port_open = _stream_port_open()
        mine = stream_proc is not None and stream_proc.poll() is None
        if port_open:
            stream_status = "▶ RUNNING" if mine else "▶ RUNNING (external)"
        else:
            stream_status = "■ STOPPED"
        print(f"  Stream:     {stream_status}  rtsp://{_local_ip()}:8554/eyes")
        print()
        print("  ── Recent Output ──")
        combined = pm.out[-6:] + stream_out[-6:]
        if combined:
            for line in combined:
                print(f"  {line}")
        else:
            print("  (no output)")

        if port_open and not pm.running():
            print()
            print("  ! Eyes app is not running - the stream will be blank.")
            print("    Press [s] to start the app (the stream re-binds automatically).")
        print()
        if designs:
            print("  ── Variants ──")
            for i, name in enumerate(designs):
                row = f"    {design_label(i)}  {name}"
                print(f"{row:<26}<-" if i == current else row)
            print()
        print("  Commands:")
        print(f"    [{design_range(designs)}]  Select design by number, or type its name")
        print("    [m]    Toggle mode")
        print("    [f]    Toggle fullscreen")
        print("    [l]    Toggle LDR sensor")
        print("    [t]    Toggle RTSP stream")
        print("    [s]    Start process")
        print("    [x]    Stop process")
        print("    [r]    Restart process")
        print("    [q]    Quit")
        print()
        try:
            cmd = input("  > ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            break

        if cmd == "q":
            break
        elif cmd == "m":
            mode = 1 - mode
        elif cmd == "f":
            fullscreen = not fullscreen
        elif cmd == "l":
            ldr_on = not ldr_on
        elif cmd == "t":
            if stream_proc and stream_proc.poll() is None:
                stream_proc.terminate()
                try:
                    stream_proc.wait(timeout=3)
                except:
                    stream_proc.kill()
                stream_proc = None
                print("  RTSP stream stopped")
            else:
                rtsp_script = PROJECT_DIR / "rtsp_server.py"
                if rtsp_script.exists():
                    # Let the server resolve the eye window itself: it re-binds
                    # automatically when the app restarts, and parsing an XID
                    # here is error prone (the geometry "1280x720" contains a
                    # "0x720" substring that a naive grep will match).
                    env = {**os.environ, "DISPLAY": os.environ.get("DISPLAY", ":0")}
                    stream_proc = subprocess.Popen(
                        ["/usr/bin/python3", "-u", str(rtsp_script)],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        stdin=subprocess.DEVNULL,
                        env=env,
                    )
                    threading.Thread(target=_drain, args=(stream_proc,), daemon=True).start()
                    stream_out = [f"PID {stream_proc.pid}"]
                    print(f"  RTSP stream started (PID {stream_proc.pid})")
                    print(f"  VLC URL: rtsp://{_local_ip()}:8554/eyes")
                else:
                    print("  ERROR: rtsp_server.py not found")
                time.sleep(1)
        elif cmd == "s":
            args = ["--design", designs[current], "--borderless", "--background", "0,0,0"]
            if fullscreen:
                args.append("--fullscreen")
            if ldr_on:
                args += ["--ldr", "--ldr-port", ldr_port]
            ok, msg = pm.start(args)
            print(f"  {'OK' if ok else 'ERR'}: {msg}")
            time.sleep(1)
        elif cmd == "x":
            pm.stop()
            print("  Stopped")
            time.sleep(0.5)
        elif cmd == "r":
            if pm.running():
                args = ["--design", designs[current], "--borderless", "--background", "0,0,0"]
                if fullscreen:
                    args.append("--fullscreen")
                if ldr_on:
                    args += ["--ldr", "--ldr-port", ldr_port]
                pm.stop()
                time.sleep(0.3)
                ok, msg = pm.start(args)
                print(f"  {'OK' if ok else 'ERR'}: {msg}")
                time.sleep(1)
        else:
            # Not a known single-key command: treat as a design selection,
            # either a 1-2 digit index or a design name / unique prefix.
            idx, err = resolve_design(cmd, designs)
            if idx is not None:
                if idx != current:
                    current = idx
                    print(f"  Design -> {design_label(current)} {designs[current]}")
                    if pm.running():
                        print("  (running: press [r] to apply, or [s] after [x])")
                else:
                    print(f"  Already on {design_label(current)} {designs[current]}")
            elif err:
                print(f"  {err}")
            else:
                print(f"  Unknown command {cmd!r}. Try [{design_range(designs)}],"
                      " a design name, or [q] to quit.")

    pm.stop()
    if stream_proc and stream_proc.poll() is None:
        stream_proc.terminate()
        try:
            stream_proc.wait(timeout=3)
        except:
            stream_proc.kill()
    log("Goodbye!")


if __name__ == "__main__":
    main()
