#!/usr/bin/env python3
"""Uncanny Eyes TUI Controller.

Design selection is index-based: the variant list is discovered from the binary
at startup via `--list-designs`, so it always matches the compiled registry.
Adding a variant means editing the C++ registry only -- never this file.
Restart the TUI after a rebuild to pick up new variants.

The mode, iris-type, pupil-type and realism vocabularies are likewise read from
the binary (`--list-options`) so this file cannot advertise a value the build
does not accept.
"""
import glob, os, subprocess, sys, threading, time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent


def resolve_binary():
    """Prefer the freshest executable that is actually built.

    The project has multiple build trees; the build-dir binary is the canonical
    live renderer, while the LDR tree is stale if it has not been rebuilt after a
    new artwork variant is generated. The TUI must follow the current registry,
    not a stale binary path.
    """
    candidates = [
        PROJECT_DIR / "build" / "uncanny-eyes",
        PROJECT_DIR / "build-ldr" / "uncanny-eyes",
        PROJECT_DIR / "build-debug" / "uncanny-eyes",
    ]
    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate
    return PROJECT_DIR / "build-ldr" / "uncanny-eyes"


BINARY = resolve_binary()

# Single-character commands. Anything else typed at the prompt is treated as a
# design selection (2-digit index or design name) or a parameter value.
COMMANDS = set("mflt") | set("srxq") | set("edca")

# Fallbacks used only when the binary is too old to report its own vocabulary.
FALLBACK_OPTIONS = {
    "modes": ["normal", "dozing", "suspicious", "angry"],
    "iris_types": ["mammalian", "feline", "reptilian", "goat"],
    "pupil_types": ["round", "slit", "horizontal-slit"],
    "realism": ["stylized", "realistic", "hyperreal"],
}

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


def _start_xvfb():
    """Start Xvfb on the virtual display if not already running."""
    global xvfb_proc
    if xvfb_proc and xvfb_proc.poll() is None:
        return  # already running
    try:
        # Xvfb :99 -screen 0 1920x1080x24 -ac +extension GLX +render -noreset
        xvfb_proc = subprocess.Popen(
            ["Xvfb", xvfb_display, "-screen", "0", "1920x1080x24", "-ac", "+extension", "GLX", "+render", "-noreset"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
        )
        threading.Thread(target=_drain, args=(xvfb_proc,), daemon=True).start()
        time.sleep(0.5)  # give Xvfb time to start
        log(f"Xvfb started on {xvfb_display}")
    except Exception as e:
        log(f"Failed to start Xvfb: {e}")
        xvfb_proc = None


def _stop_xvfb():
    """Stop Xvfb if running."""
    global xvfb_proc
    if xvfb_proc:
        try:
            xvfb_proc.terminate()
            xvfb_proc.wait(timeout=3)
        except Exception:
            xvfb_proc.kill()
        xvfb_proc = None
        log(f"Xvfb on {xvfb_display} stopped")

def find_ldr_port():
    """Auto-detect the LDR serial port."""
    for pattern in ["/dev/ttyUSB*", "/dev/ttyACM*"]:
        ports = sorted(glob.glob(pattern))
        for p in ports:
            if os.path.exists(p):
                return p
    return None


def _run_listing(flag):
    """Ask the binary for a newline-separated vocabulary list."""
    try:
        out = subprocess.run(
            [str(BINARY), flag],
            capture_output=True, text=True, timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as e:
        log(f"could not run {BINARY} {flag}: {e}")
        return []
    if out.returncode != 0:
        log(f"{flag} exited {out.returncode}: {out.stderr.strip()}")
        return []
    return [line.strip() for line in out.stdout.splitlines() if line.strip()]


def load_designs():
    """Ask the binary which designs it has.

    The list is whatever artwork_registry.cpp contains, so it cannot drift out
    of sync with the build the way a hardcoded list does.
    """
    return _run_listing("--list-designs")


def load_options():
    """Read the mode / iris / pupil / realism vocabularies from the binary.

    Falling back to a hardcoded list here would reintroduce exactly the drift
    this file avoids for designs, so a failure is reported rather than silently
    substituted -- the fallback is only used when the binary predates the flag.
    """
    options = {}
    for key, flag in (("modes", "--list-modes"), ("iris_types", "--list-iris-types"),
                      ("pupil_types", "--list-pupil-types"),
                      ("realism", "--list-realism")):
        values = _run_listing(flag)
        if values:
            options[key] = values
        else:
            options[key] = list(FALLBACK_OPTIONS[key])
            log(f"{flag} unavailable; using built-in {key}")
    return options


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


def resolve_option(raw, values):
    """Resolve typed text to an index in a vocabulary, by index or name."""
    text = raw.strip().lower()
    if not text or not values:
        return None, None
    if text.isdigit():
        idx = int(text) - 1
        if 0 <= idx < len(values):
            return idx, None
        return None, f"no option {text} (have 1-{len(values)})"
    matches = [i for i, name in enumerate(values) if name.lower().startswith(text)]
    if len(matches) == 1:
        return matches[0], None
    if not matches:
        return None, f"no option matching {text!r} (have: {', '.join(values)})"
    return None, "ambiguous: " + ", ".join(values[i] for i in matches)


def parse_float(raw, lo, hi, current):
    """Parse a numeric parameter, reporting the valid range on error."""
    try:
        value = float(raw)
    except ValueError:
        return None, f"{raw!r} is not a number"
    if not (lo <= value <= hi):
        return None, f"value {value} is outside {lo}..{hi} (current {current})"
    return value, None

class PM:
    def __init__(self):
        self.proc = None
        self.out = []
        self.pid = None
        self.args = []
        self.restarts = 0

    def start(self, args, display=None):
        self.stop()
        env = dict(os.environ)
        if display is not None:
            env["DISPLAY"] = display
        try:
            self.proc = subprocess.Popen(
                [str(BINARY)] + args,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL, env=env
            )
            self.pid = self.proc.pid
            self.args = list(args)
            self.restarts += 1
            self.out = [f"PID {self.pid}"]
            threading.Thread(target=self._read, daemon=True).start()
            return True, f"Started (PID {self.pid})"
        except Exception as e:
            return False, str(e)

    def stop(self):
        if self.proc:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=3)
            except Exception:
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
    log(f"=== TUI using binary: {BINARY} ===")
    pm = PM()
    designs = load_designs()
    log(f"loaded designs: {designs}")
    options = load_options()
    modes = options["modes"]
    iris_types = options["iris_types"]
    pupil_types = options["pupil_types"]
    realism = options["realism"]
    if not designs:
        log(f"ERROR: no designs found. Is {BINARY} built?")
    else:
        log(f"{len(designs)} designs: " + ", ".join(
            f"{design_label(i)}={n}" for i, n in enumerate(designs)))
    log(f"modes: {', '.join(modes)}")

    current = 0
    mode = 0
    iris_type = 0
    pupil_type = 0
    pupil_type_explicit = False
    real = 1 if len(realism) > 1 else 0
    expression_intensity = 1.0
    debug_iris_state = 0  # 0=normal, 1=min, 2=max
    ldr_sensitivity = 1.0
    ldr_min_pupil = 0.25
    ldr_max_pupil = 1.0
    ldr_invert = False
    ldr_curve = 0.45
    fullscreen = False
    stream_mode = False
    face_overlay_on = False
    ldr_on = False
    ldr_port = find_ldr_port() or "/dev/ttyUSB0"
    stream_proc = None
    message = ""
    port_warning = False
    headless = False          # True = render on Xvfb :99 (no local eyes), False = render on :0
    xvfb_proc = None
    xvfb_display = ":99"

    def build_args():
        """Assemble the eye-process argument list from the current settings.

        Built in one place so [s] and [r] can never disagree about what is
        being launched -- which previously let a restart silently drop every
        setting the operator had changed.

        When headless is on, the eyes render on a virtual framebuffer (:99)
        instead of the physical display -- the RTSP stream is unaffected.
        """
        args = ["--design", designs[current] if designs else "default"]
        if debug_iris_state == 1:
            args += ["--iris-scale", "min"]
        elif debug_iris_state == 2:
            args += ["--iris-scale", "max"]
        if stream_mode or headless:
            args.append("--stream-mode")
        else:
            args.append("--borderless")
            args += ["--background", "0,0,0"]
        if face_overlay_on:
            args += ["--face-overlay", "face_overlay.png"]
        if fullscreen:
            args.append("--fullscreen")
        args += ["--mode", modes[mode]]
        args += ["--iris-type", iris_types[iris_type],
                 "--realism", realism[real],
                 "--expression-intensity", f"{expression_intensity:.2f}"]
        if pupil_type_explicit:
            args += ["--pupil-type", pupil_types[pupil_type]]
        if ldr_on:
            args += ["--ldr", "--ldr-port", ldr_port,
                     "--ldr-sensitivity", f"{ldr_sensitivity:.2f}",
                     "--ldr-min-pupil", f"{ldr_min_pupil:.2f}",
                     "--ldr-max-pupil", f"{ldr_max_pupil:.2f}",
                     "--ldr-curve", f"{ldr_curve:.2f}"]
            if ldr_invert:
                args.append("--ldr-invert")
        return args

    def active_display():
        """Return the DISPLAY value for the current rendering target."""
        return xvfb_display if headless else os.environ.get("DISPLAY", ":0")

    def restart_if_running():
        """Re-launch with the current settings when the app is live."""
        nonlocal message
        if not pm.running():
            return
        pm.stop()
        time.sleep(0.3)
        ok, msg = pm.start(build_args(), active_display())
        message = f"{'OK' if ok else 'ERR'}: {msg}"

    while True:
        os.system("clear")
        print("=" * 56)
        print("  UNCANNY EYES CONTROLLER")
        print("=" * 56)
        current_name = designs[current] if designs else "(none)"
        print(f"  Design:     {design_label(current)} {current_name}")
        print(f"  Mode:       {modes[mode]}")
        print(f"  Iris type:  {iris_types[iris_type]}")
        print(f"  Pupil type: {pupil_types[pupil_type]}"
              + ("" if pupil_type_explicit else "  (from artwork)"))
        print(f"  Realism:    {realism[real]}")
        debug_label = ["normal", "min", "max"][debug_iris_state]
        print(f"  Debug iris: {debug_label}")
        print(f"  Expression: {expression_intensity:.2f}"
              f"   LDR sens: {ldr_sensitivity:.2f}")
        print(f"  Fullscreen: {'Yes' if fullscreen else 'No'}"
              f"   Stream mode: {'Yes' if stream_mode else 'No'}")
        print(f"  Face overlay: {'Yes' if face_overlay_on else 'No'}")
        print(f"  LDR:        {'ON' if ldr_on else 'OFF'} ({ldr_port})")
        print(f"  Calibration: min-pupil {ldr_min_pupil:.2f}"
              f"  max-pupil {ldr_max_pupil:.2f}"
              f"  curve {ldr_curve:.2f}"
              f"  {'inverted' if ldr_invert else 'normal'}")
        print(f"  Process:    {'▶ RUNNING' if pm.running() else '■ STOPPED'}"
              f"  (restarts: {pm.restarts})")

        # Reflect reality: the stream may have been started outside this TUI
        # (e.g. by run_rtsp.sh), so the listening port is the source of truth.
        port_open = _stream_port_open()
        mine = stream_proc is not None and stream_proc.poll() is None
        if port_open:
            stream_status = "▶ RUNNING" if mine else "▶ RUNNING (external)"
        else:
            stream_status = "■ STOPPED"
        print(f"  Stream:     {stream_status}  rtsp://{_local_ip()}:8554/eyes")

        if message:
            print()
            print(f"  > {message}")
            message = ""

        warnings = collect_warnings(pm, designs, stream_mode, port_open,
                                   ldr_on, ldr_port, ldr_invert,
                                   ldr_min_pupil, ldr_max_pupil, pupil_type_explicit)
        if warnings:
            print()
            print("  ── Warnings ──")
            for warning in warnings:
                print(f"  ! {warning}")

        print()
        print("  ── Recent Output ──")
        combined = pm.out[-6:]
        if combined:
            for line in combined:
                print(f"  {line}")
        else:
            print("  (no output)")

        print()
        if designs:
            print("  ── Variants ──")
            for i, name in enumerate(designs):
                row = f"    {design_label(i)}  {name}"
                print(f"{row:<26}<-" if i == current else row)
            print()
        print("  Commands:")
        print(f"    [{design_range(designs)}]  Select design, or type its name")
        print(f"    [m]    Next mode          ({len(modes)}: {', '.join(modes)})")
        print("    [d]    Debug iris cycle   normal / min / max")
        print("    [e]    Next iris type     " + ", ".join(iris_types))
        print("    [p]    Next pupil type    " + ", ".join(pupil_types))
        print("    [c]    Next realism level " + ", ".join(realism))
        print("    [a]    Toggle pupil-type override (use asset geometry)")
        print("    [f]    Toggle fullscreen")
        print("    [v]    Toggle 16:9 borderless stream mode")
        print("    [o]    Toggle face overlay PNG")
        print("    [l]    Toggle LDR sensor")
        print("    [i]    Toggle LDR invert")
        print("    [t]    Toggle RTSP stream")
        print("    [s]    Start process")
        print("    [x]    Stop process")
        print("    [r]    Restart process (re-applies all settings)")
        print("    [q]    Quit")
        print("    Tuning (applies on restart):")
        print("      expression <0..2>   ldr-sens <0..4>   min-pupil <0..1>")
        print("      max-pupil <0..1>    ldr-curve <0..4>")
        print()
        try:
            raw = input("  > ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        cmd = raw.lower()

        if cmd == "q":
            break
        elif cmd == "m":
            mode = (mode + 1) % len(modes)
            restart_if_running()
        elif cmd == "d":
            debug_iris_state = (debug_iris_state + 1) % 3
            restart_if_running()
        elif cmd == "e":
            iris_type = (iris_type + 1) % len(iris_types)
            restart_if_running()
        elif cmd == "p":
            pupil_type = (pupil_type + 1) % len(pupil_types)
            pupil_type_explicit = True
            restart_if_running()
        elif cmd == "c":
            real = (real + 1) % len(realism)
            restart_if_running()
        elif cmd == "a":
            pupil_type_explicit = not pupil_type_explicit
            restart_if_running()
        elif cmd == "f":
            fullscreen = not fullscreen
            restart_if_running()
        elif cmd == "v":
            stream_mode = not stream_mode
            restart_if_running()
        elif cmd == "o":
            face_overlay_on = not face_overlay_on
            restart_if_running()
        elif cmd == "l":
            ldr_on = not ldr_on
            restart_if_running()
        elif cmd == "i":
            ldr_invert = not ldr_invert
            restart_if_running()
        elif cmd == "s":
            ok, msg = pm.start(build_args())
            message = f"{'OK' if ok else 'ERR'}: {msg}"
            time.sleep(1)
        elif cmd == "x":
            pm.stop()
            message = "Stopped"
            time.sleep(0.5)
        elif cmd == "r":
            if pm.running():
                pm.stop()
                time.sleep(0.3)
                ok, msg = pm.start(build_args())
                message = f"{'OK' if ok else 'ERR'}: {msg}"
                time.sleep(1)
            else:
                message = "Not running; press [s] to start."
        elif cmd == "t":
            if stream_proc and stream_proc.poll() is None:
                stream_proc.terminate()
                try:
                    stream_proc.wait(timeout=3)
                except Exception:
                    stream_proc.kill()
                stream_proc = None
                message = "RTSP stream stopped"
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
                    message = f"RTSP stream started (PID {stream_proc.pid})"
                    time.sleep(1)
                else:
                    message = "ERROR: rtsp_server.py not found"
        elif cmd.startswith("expression"):
            value, err = parse_float(cmd.partition(" ")[2].strip(), 0.0, 2.0,
                                     expression_intensity)
            if err:
                message = f"expression: {err}"
            else:
                expression_intensity = value
                restart_if_running()
        elif cmd.startswith("ldr-sens"):
            value, err = parse_float(cmd.partition(" ")[2].strip(), 0.01, 4.0,
                                     ldr_sensitivity)
            if err:
                message = f"ldr-sens: {err}"
            else:
                ldr_sensitivity = value
                restart_if_running()
        elif cmd.startswith("min-pupil"):
            value, err = parse_float(cmd.partition(" ")[2].strip(), 0.0, 1.0,
                                     ldr_min_pupil)
            if err:
                message = f"min-pupil: {err}"
            else:
                ldr_min_pupil = value
                restart_if_running()
        elif cmd.startswith("max-pupil"):
            value, err = parse_float(cmd.partition(" ")[2].strip(), 0.0, 1.0,
                                     ldr_max_pupil)
            if err:
                message = f"max-pupil: {err}"
            else:
                ldr_max_pupil = value
                restart_if_running()
        elif cmd.startswith("ldr-curve"):
            value, err = parse_float(cmd.partition(" ")[2].strip(), 0.01, 4.0,
                                     ldr_curve)
            if err:
                message = f"ldr-curve: {err}"
            else:
                ldr_curve = value
                restart_if_running()
        else:
            # Not a known command: treat as a design or vocabulary selection,
            # either by index or by unique name prefix.
            for values, name in ((modes, "mode"), (iris_types, "iris type"),
                                 (pupil_types, "pupil type"), (realism, "realism"),
                                 (designs, "design")):
                idx, err = resolve_option(cmd, values)
                if idx is None:
                    if err and "ambiguous" in err:
                        message = f"{name}: {err}"
                    continue
                if name == "design":
                    current = idx
                    pupil_type_explicit = False
                elif name == "mode":
                    mode = idx
                elif name == "iris type":
                    iris_type = idx
                elif name == "pupil type":
                    pupil_type = idx
                    pupil_type_explicit = True
                else:
                    real = idx
                message = f"{name} -> {values[idx]}"
                restart_if_running()
                break
            else:
                message = (f"Unknown command {cmd!r}. Try [{design_range(designs)}],"
                           " a design/mode/type name, or [q] to quit.")

    pm.stop()
    if stream_proc and stream_proc.poll() is None:
        stream_proc.terminate()
        try:
            stream_proc.wait(timeout=3)
        except Exception:
            stream_proc.kill()
    log("Goodbye!")


def collect_warnings(pm, designs, stream_mode, port_open, ldr_on, ldr_port,
                     ldr_invert, ldr_min_pupil, ldr_max_pupil, pupil_type_explicit):
    """Operator-facing warnings for states that will look wrong on screen."""
    warnings = []
    if port_open and not pm.running():
        warnings.append("eyes app is not running - the stream will be blank "
                        "(press [s])")
    if stream_mode and not port_open:
        warnings.append("stream mode is on but no RTSP server is listening "
                        "(press [t])")
    if ldr_on and not os.path.exists(ldr_port):
        warnings.append(f"LDR is on but {ldr_port} does not exist")
    if ldr_on and ldr_min_pupil >= ldr_max_pupil:
        warnings.append("calibration is inverted: min-pupil must be below "
                        "max-pupil, so the pupil will not respond to light")
    if ldr_invert:
        warnings.append("LDR invert is on - use this only if bright light "
                        "lowers the ADC value")
    if pupil_type_explicit and designs:
        warnings.append("pupil-type override is on; the artwork's own pupil "
                        "geometry is being ignored")
    if pm.running():
        recent = pm.out[-12:]
        for line in recent:
            lowered = line.lower()
            if "unknown" in lowered and "design" in lowered:
                warnings.append(f"app reported: {line}")
            elif "device or resource busy" in lowered:
                warnings.append("serial port is held by another process "
                                "(close the serial monitor)")
    return warnings


if __name__ == "__main__":
    main()
