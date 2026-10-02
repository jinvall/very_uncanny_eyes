#!/usr/bin/env python3
"""Serial Monitor for LDR sensor - auto-detects port."""
import glob, re, serial, sys, time

def find_port():
    for p in sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")):
        if __import__("os").path.exists(p):
            return p
    return None

def main():
    port = find_port()
    if not port:
        print("ERROR: No serial port found", file=sys.stderr)
        sys.exit(1)
    print(f"Connecting to {port} @ 115200 baud...")
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"Connected! Watching for LDR data...\n")
        print(f"{'Time':<12} {'Raw':>6} {'Lux':>8} {'Pupil':>8}")
        print("-" * 40)
        while True:
            line = ser.readline().decode(errors="replace").strip()
            if not line:
                continue
            m = re.match(r'LDR:(\d+)', line)
            if m:
                raw = int(m.group(1))
                lux = raw * 1000 / 1023
                pupil = 0.25 + (1 - raw/1023) * 0.75
                ts = time.strftime("%H:%M:%S")
                print(f"{ts:<12} {raw:>6} {lux:>8.1f} {pupil:>8.2f}")
            else:
                print(f"{time.strftime('%H:%M:%S'):<12} {line}")
    except serial.SerialException as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nStopped")

if __name__ == "__main__":
    main()
