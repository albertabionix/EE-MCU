"""
plot_encoder.py — live graph of the AMT132S encoder data from the Nucleo.

What it does:
  - Reads the CSV lines the board sends over USB (COM4 by default).
  - Draws two live graphs: joint angle vs time, and joint speed vs time.
  - Saves every line to a CSV file (encoder_YYYYMMDD_HHMMSS.csv) that you
    can open in Excel later.

Setup (once):
  1. Install Python from python.org (tick "Add python.exe to PATH").
  2. In a terminal (Windows: search "cmd"):   pip install pyserial matplotlib

Run:
  python plot_encoder.py            (uses COM4)
  python plot_encoder.py COM5       (if your port is different)

Close PuTTY first: only one program can use the COM port at a time.
Close the graph window to stop; the CSV file is saved automatically.
"""

import collections
import csv
import sys
import time

import matplotlib.pyplot as plt
import serial
from matplotlib.animation import FuncAnimation

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
BAUD = 115200
WINDOW_S = 10.0          # how many seconds of history the graph shows

# --- Open the serial port and the output file --------------------------------
ser = serial.Serial(PORT, BAUD, timeout=0.05)
out_name = time.strftime("encoder_%Y%m%d_%H%M%S.csv")
out_file = open(out_name, "w", newline="")
writer = csv.writer(out_file)
writer.writerow(["time_s", "pos_counts", "joint_deg", "joint_speed_degs", "rpm"])
print(f"Reading {PORT} at {BAUD} baud, saving to {out_name}")

# Recent data for the graph (deque = list that's fast to trim from the front)
t_buf = collections.deque()
ang_buf = collections.deque()
spd_buf = collections.deque()
t0_ms = None


def read_available_lines():
    """Read every complete line waiting on the port, store and log it."""
    global t0_ms
    while ser.in_waiting:
        raw = ser.readline().decode(errors="ignore").strip()
        parts = raw.split(",")
        if len(parts) != 5:
            continue                      # header line or a partial line
        try:
            t_ms = int(parts[0])
            pos = int(parts[1])
            ang = float(parts[2])
            spd = float(parts[3])
            rpm = float(parts[4])
        except ValueError:
            continue                      # garbled line, skip it

        if t0_ms is None:
            t0_ms = t_ms                  # time 0 = first line received
        t_s = (t_ms - t0_ms) / 1000.0

        writer.writerow([f"{t_s:.3f}", pos, ang, spd, rpm])
        t_buf.append(t_s)
        ang_buf.append(ang)
        spd_buf.append(spd)

    # Keep only the last WINDOW_S seconds on screen
    while t_buf and t_buf[-1] - t_buf[0] > WINDOW_S:
        t_buf.popleft()
        ang_buf.popleft()
        spd_buf.popleft()


# --- Figure: angle on top, speed below, same time axis -----------------------
fig, (ax_ang, ax_spd) = plt.subplots(2, 1, sharex=True, figsize=(9, 6))
(line_ang,) = ax_ang.plot([], [], lw=1.5)
(line_spd,) = ax_spd.plot([], [], lw=1.5, color="tab:orange")
ax_ang.set_ylabel("Joint angle (deg)")
ax_spd.set_ylabel("Joint speed (deg/s)")
ax_spd.set_xlabel("Time (s)")
ax_ang.grid(True, alpha=0.3)
ax_spd.grid(True, alpha=0.3)
ax_spd.axhline(0, color="gray", lw=0.8)
fig.suptitle("AMT132S encoder — live")


def update(_frame):
    read_available_lines()
    if t_buf:
        line_ang.set_data(t_buf, ang_buf)
        line_spd.set_data(t_buf, spd_buf)
        for ax in (ax_ang, ax_spd):
            ax.relim()
            ax.autoscale_view()
        ax_spd.set_xlim(max(0.0, t_buf[-1] - WINDOW_S), max(WINDOW_S, t_buf[-1]))
    return line_ang, line_spd


anim = FuncAnimation(fig, update, interval=50, cache_frame_data=False)

try:
    plt.tight_layout()
    plt.show()                            # blocks until you close the window
finally:
    out_file.close()
    ser.close()
    print(f"Saved {out_name}")
