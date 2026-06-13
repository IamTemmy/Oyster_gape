#!/usr/bin/env python3
"""
sweep_logger.py  --  Oyster_gape host logger / serial passthrough

Replaces CoolTerm. It connects to the ESP32 over USB serial, forwards what you
type, shows what the board prints, and AUTOMATICALLY saves each characterization
sweep as a CSV plus a PNG plot in the data/ folder -- ready to share or print.

USAGE
    python sweep_logger.py [--port PORT] [--range LABEL] [--outdir data]

  If --port is omitted it auto-detects a USB serial device.
  Set the magnetic-range label (used in the filenames) with --range, e.g.
        python sweep_logger.py --range as_shipped
  or change it live before a run by typing:   :range 50mT

LOCAL COMMANDS (handled here, not sent to the board) -- start with ':'
    :range LABEL   set the range label for the next saved files
    :port          show the connected port
    :quit          exit
  Anything else you type (200, read, sweep, ...) is sent to the ESP32.

Each completed sweep is saved as:
    data/charac_<label>_run<N>.csv   and   data/charac_<label>_run<N>.png
with N auto-incremented so nothing is overwritten.

REQUIRES:  pyserial, matplotlib
    pip install pyserial matplotlib      (or: conda install pyserial matplotlib)
"""

import argparse, os, sys, threading, time, datetime
import serial
from serial.tools import list_ports

import matplotlib
matplotlib.use("Agg")            # save PNGs without needing a display
import matplotlib.pyplot as plt


def autodetect_port():
    ports = list(list_ports.comports())
    pref = [p.device for p in ports
            if any(k in (p.device or "") for k in
                   ("usbserial", "SLAB", "wchusb", "usbmodem"))]
    if pref:
        return pref[0]
    others = [p.device for p in ports
              if p.device and "/cu." in p.device and "Bluetooth" not in p.device]
    return others[0] if others else None


class SweepCapture:
    """Watches the serial stream and saves each sweep block to CSV + PNG."""
    def __init__(self, outdir, range_label):
        self.outdir = outdir
        self.range_label = range_label
        self.active = False
        self.meta = []
        self.rows = []
        os.makedirs(outdir, exist_ok=True)

    def feed(self, line):
        s = line.strip()
        if s.startswith("# --- characterization sweep"):
            self.active = True
            self.meta = [s]
            self.rows = []
            return
        if not self.active:
            return
        if s.startswith("# Sweep complete"):
            self._finish()
            self.active = False
            return
        if s.startswith("#"):
            self.meta.append(s)
            return
        if s.lower().startswith("distance_mm"):
            return                       # header line; we write our own
        parts = s.split(",")
        if len(parts) >= 2:
            try:
                d = float(parts[0]); r = float(parts[1])
                sd = float(parts[2]) if len(parts) > 2 else 0.0
                self.rows.append((d, r, sd))
            except ValueError:
                pass                     # ignore stray non-numeric lines

    def _next_path(self, ext):
        base = f"charac_{self.range_label}_run"
        n = 1
        while os.path.exists(os.path.join(self.outdir, f"{base}{n}.{ext}")):
            n += 1
        return os.path.join(self.outdir, f"{base}{n}.{ext}"), n

    def _finish(self):
        if not self.rows:
            print("[logger] sweep ended but captured no data rows.")
            return
        csv_path, n = self._next_path("csv")
        with open(csv_path, "w") as f:
            f.write(f"# saved {datetime.datetime.now().isoformat(timespec='seconds')}\n")
            f.write(f"# range_label={self.range_label}\n")
            for m in self.meta:
                f.write(m + "\n")
            f.write("Distance_mm,Reading_mV,SD_mV\n")
            for d, r, sd in self.rows:
                f.write(f"{d:.3f},{r:.0f},{sd:.2f}\n")
        png_path = csv_path[:-4] + ".png"
        self._plot(png_path)
        print(f"[logger] SAVED  {csv_path}")
        print(f"[logger] SAVED  {png_path}   ({len(self.rows)} points)")

    def _plot(self, path):
        d  = [x[0] for x in self.rows]
        r  = [x[1] for x in self.rows]
        sd = [x[2] for x in self.rows]
        lo = [a - b for a, b in zip(r, sd)]
        hi = [a + b for a, b in zip(r, sd)]
        plt.figure(figsize=(8, 5))
        plt.fill_between(d, lo, hi, alpha=0.25, label="±SD (noise)")
        plt.plot(d, r, lw=1.6, label="sensor output")
        plt.xlabel("Distance from start (mm)")
        plt.ylabel("Sensor output (mV)")
        plt.title(f"HAL 2425 characterization — {self.range_label}")
        plt.grid(True, alpha=0.3)
        plt.legend()
        plt.tight_layout()
        plt.savefig(path, dpi=150)
        plt.close()


def reader_loop(ser, cap, stop):
    buf = b""
    while not stop.is_set():
        try:
            data = ser.read(256)
        except Exception:
            break
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = raw.decode("utf-8", "replace").rstrip("\r")
            print(text)
            cap.feed(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--range", dest="range_label", default="unset")
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    port = args.port or autodetect_port()
    if not port:
        print("No serial port found. Pass it explicitly, e.g.:")
        print("  python sweep_logger.py --port /dev/cu.usbserial-XXXX")
        sys.exit(1)

    ser = serial.Serial(port, args.baud, timeout=0.1)
    time.sleep(0.3)
    cap = SweepCapture(args.outdir, args.range_label)
    print(f"[logger] connected {port} @ {args.baud}.  range label = '{cap.range_label}'")
    print("[logger] type board commands (200, read, sweep) or :range / :port / :quit")

    stop = threading.Event()
    t = threading.Thread(target=reader_loop, args=(ser, cap, stop), daemon=True)
    t.start()

    try:
        for line in sys.stdin:
            cmd = line.rstrip("\n")
            if cmd.startswith(":"):
                parts = cmd[1:].split()
                if not parts:
                    continue
                if parts[0] == "quit":
                    break
                elif parts[0] == "range" and len(parts) > 1:
                    cap.range_label = parts[1]
                    print(f"[logger] range label set to '{cap.range_label}'")
                elif parts[0] == "port":
                    print(f"[logger] {port}")
                else:
                    print("[logger] unknown local command (:range / :port / :quit)")
            else:
                ser.write((cmd + "\n").encode())
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        time.sleep(0.2)
        ser.close()
        print("\n[logger] closed.")


if __name__ == "__main__":
    main()
