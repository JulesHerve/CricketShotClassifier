#!/usr/bin/env python3
"""
CricketIQ -- Raspberry Pi 5 + Sense HAT collector
=================================================
Runs ON the Pi with a Sense HAT on the GPIO header. Nothing else needed: the
HAT's IMU is the sensor, the joystick is the input, the 8x8 LED matrix is the
display. Fully headless -- no monitor, no keyboard, no serial.

Works with BOTH Sense HAT generations. It reads WHO_AM_I at boot and picks
the driver automatically:
    0x68 -> LSM9DS1  (original Sense HAT)
    0x6A -> LSM6DSL  (Sense HAT V2)
Both are configured for +/-16 g and +/-2000 dps so the data is comparable.

CSV schema (columns unchanged from the earlier builds):
    Recording ID, Timestamp, Acceleration X, Acceleration Y, Acceleration Z,
    Gyroscope X, Gyroscope Y, Gyroscope Z, Shot Type
Timestamp = Pi monotonic microseconds. Accel in g, gyro in deg/s.

LAYOUT: rows are grouped by shot type (all Drives, then Pulls, then Cuts,
then Stationary), and Recording ID restarts at 1 within each group -- so you
get Drive 1..50, Pull 1..50, and so on.

    >>> IMPORTANT: Recording ID is therefore NOT unique on its own.
    >>> Drive 1 and Pull 1 are different shots. The unique key is the PAIR
    >>> (Shot Type, Recording ID). In pandas, window your data with
    >>>     df.groupby(["Shot Type", "Recording ID"])
    >>> and NOT df.groupby("Recording ID"), which would fuse four different
    >>> shots into one sample.

Numbers are assigned at export time, so undoing a shot renumbers that type
automatically and never leaves a hole in the sequence.

All output is plain ASCII so it stays readable on a terminal that isn't set
to UTF-8.

----------------------------------------------------------------------------
WHY THIS DOESN'T USE THE `sense_hat` LIBRARY

`sense_hat` routes the IMU through RTIMULib, which is the most breakage-prone
piece of the stack (`import RTIMU` fails in venvs and off-image) and gives no
control over full-scale range or output data rate -- the two things that
matter most for capturing impacts. So instead:

    IMU        -> direct I2C register access at 0x6A
    LED matrix -> direct write to the "RPi-Sense FB" RGB565 framebuffer
    Joystick   -> direct read of the "Sense HAT Joystick" evdev node

Only dependency: smbus2.

----------------------------------------------------------------------------
SETUP ON THE PI

    sudo apt update
    sudo apt install -y sense-hat i2c-tools
    pip install smbus2
    sudo reboot

    i2cdetect -y 1      # expect 6a (IMU) and 1c; 46 shows as UU

    Optional, lowers read jitter -- add to /boot/firmware/config.txt, reboot:
        dtparam=i2c_arm_baudrate=400000

RUN
    python3 cricket_collect_sensehat.py --selftest      # check the HAT first
    python3 cricket_collect_sensehat.py                 # timestamped CSV
    python3 cricket_collect_sensehat.py --out alice     # resume/append
    python3 cricket_collect_sensehat.py --rate 50 --samples 100   # override
    python3 cricket_collect_sensehat.py --idle bars     # always show the tally

WHERE THE FILES GO

    Everything is written to      ~/Cricket CSV Files/
    ...no matter which directory you launch the script from. The folder is
    created automatically if it isn't there yet.

    --out takes a bare filename and puts it in that folder, so
    "--out alice" becomes ~/Cricket CSV Files/alice.csv (the .csv is added
    for you). Give it a path instead -- "--out /tmp/test.csv" -- and that
    path is used as-is. Change the folder for a run with --dir, or edit
    OUT_DIR_NAME below to change it permanently.

RUNNING WITH NO KEYBOARD

    With --wait-for-press the program parks on a breathing green PLAY symbol
    instead of collecting straight away. Press the joystick and it starts.
    Install cricketiq.service and that happens automatically at boot, so the
    whole rig is: plug in power -> wait -> press -> collect.

        sudo cp cricketiq.service /etc/systemd/system/
        sudo systemctl daemon-reload
        sudo systemctl enable --now cricketiq
        journalctl -u cricketiq -f          # watch what it's doing

    What the matrix tells you, in order:
        breathing PLAY triangle  waiting -- press MIDDLE to begin
        green wipe + green tick  the program started (this is the confirmation)
        normal letter/count/bars a session is running
        blue tick                session ended, back to waiting
        flashing red cross       startup failed -- check journalctl

    Hold MIDDLE ends a session and returns to the waiting screen; press again
    to start the next one. The process never exits, so no keyboard is needed.

    To power the Pi down safely, hold UP for ~1 s and confirm with MIDDLE
    (a red power symbol appears). Pulling the plug instead risks corrupting
    the SD card. That needs one passwordless sudo rule:

        sudo visudo -f /etc/sudoers.d/cricketiq
        jules ALL=(ALL) NOPASSWD: /sbin/shutdown, /usr/sbin/shutdown

----------------------------------------------------------------------------
JOYSTICK CONTROLS  (the matrix shows the state, so no screen needed)

    UP / DOWN         cycle shot type  D-rive -> P-ull -> C-ut -> S-tationary
    UP      (hold 1s) shut the Pi down, after a confirm press
    MIDDLE  (press)   record one shot  (countdown on the matrix, then capture)
    MIDDLE  (hold 1s) end the session
    LEFT    (press)   undo the last recording
    LEFT    (hold 1s) show the full tally for 3 s (changes nothing)
    RIGHT   (press)   show the current window setting (read-only)
    RIGHT   (hold 1s) start a fresh CSV file (new batter/session)

----------------------------------------------------------------------------
WHAT THE LED MATRIX SHOWS

Between recordings it cycles through three frames, ~1.4 s each, all in the
current shot type's colour (Drive green / Pull blue / Cut amber / Stationary
grey):

    1. LETTER   D P C S -- which shot type is armed right now
    2. NUMBER   how many of THAT type are in the current file
                (0-99 as two digits; 100+ lights the top row as a
                 "hundreds" marker and shows the last two digits)
    3. BARS     all four counts side by side as a colour-coded bar chart,
                left to right: Drive, Pull, Cut, Stationary. Bars are scaled
                to the largest count, so a short bar means that class is
                under-represented and needs more shots.

During a recording: 3-2-1 countdown, then a dim red frame while sampling,
then a green tick (saved) or red/amber cross (rejected), then the new count
for that shot type.

Use --idle letter / count / bars to pin it to a single frame instead.
----------------------------------------------------------------------------
"""

import argparse
import csv
import fcntl
import glob
import io
import math
import os
import select
import statistics
import struct
import subprocess
import sys
import time
from datetime import datetime

try:
    from smbus2 import SMBus
except ImportError:
    sys.exit("smbus2 is not installed.  Run:  pip install smbus2")


# ============================================================================
#  CONFIG
# ============================================================================
IMU_ADDR = 0x6A                 # accel+gyro on both Sense HAT generations
REG_WHO_AM_I = 0x0F             # same register address on both chips
WHOAMI_LSM9DS1 = 0x68           # original Sense HAT
WHOAMI_LSM6DSL = 0x6A           # Sense HAT V2

# --- axis convention -------------------------------------------------------
# The Sense HAT bolts flat to the Pi; the old MPU6886 unit was on a lead
# pointing elsewhere. Treat Sense HAT data as a NEW dataset rather than
# concatenating it with the old CSVs. To realign, remap here: order picks
# which physical axis feeds each column, sign flips it.
AXIS_ORDER = (0, 1, 2)          # e.g. (1, 0, 2) swaps X and Y
AXIS_SIGN = (1.0, 1.0, 1.0)     # e.g. (1.0, -1.0, 1.0) flips Y

# --- labels + CSV schema ---------------------------------------------------
SHOTS = ["Drive", "Pull", "Cut", "Stationary"]
SHOT_GLYPH = {"Drive": "D", "Pull": "P", "Cut": "C", "Stationary": "S"}
SHOT_COLOUR = {
    "Drive":      (0, 200, 0),      # green
    "Pull":       (0, 90, 255),     # blue
    "Cut":        (255, 170, 0),    # amber
    "Stationary": (140, 140, 140),  # grey
}
HEADERS = ["Recording ID", "Timestamp",
           "Acceleration X", "Acceleration Y", "Acceleration Z",
           "Gyroscope X", "Gyroscope Y", "Gyroscope Z", "Shot Type"]

# Every CSV lands here, under the user's home directory, regardless of which
# directory the script is launched from. Override at runtime with --dir.
OUT_DIR_NAME = "Cricket CSV Files"

RATE_TOL = 0.05                 # +/-5 % of target rate counts as OK
JIT_MAX = 3.0                   # ms std-dev of sample interval
HOLD_SECONDS = 1.0              # press longer than this = "hold" action
IDLE_FLIP_SECONDS = 1.4         # how long each idle frame stays up
JOY_ROTATE_WITH_DISPLAY = True


# ============================================================================
#  IMU drivers -- direct I2C, one class per chip, same interface
# ============================================================================
def _s16(lo, hi):
    v = (hi << 8) | lo
    return v - 65536 if v >= 32768 else v


class _IMUBase:
    name = "?"
    default_accel_mg = 0.0
    gyro_mdps = 70.0

    def __init__(self, bus, addr=IMU_ADDR, accel_mg=None):
        self.bus = bus
        self.addr = addr
        self.accel_mg = accel_mg if accel_mg else self.default_accel_mg

    def _map(self, a_raw, g_raw):
        acc = [a_raw[AXIS_ORDER[i]] * self.accel_mg / 1000.0 * AXIS_SIGN[i]
               for i in range(3)]
        gyr = [g_raw[AXIS_ORDER[i]] * self.gyro_mdps / 1000.0 * AXIS_SIGN[i]
               for i in range(3)]
        return acc[0], acc[1], acc[2], gyr[0], gyr[1], gyr[2]


class LSM6DSL(_IMUBase):
    """Sense HAT V2. Gyro and accel outputs are contiguous: one 12-byte read."""
    name = "LSM6DSL (Sense HAT V2)"
    default_accel_mg = 0.488        # +/-16 g
    gyro_mdps = 70.0                # +/-2000 dps

    CTRL1_XL = 0x10
    CTRL2_G = 0x11
    CTRL3_C = 0x12
    OUTX_L_G = 0x22

    def init(self):
        b, a = self.bus, self.addr
        b.write_byte_data(a, self.CTRL3_C, 0x01)        # software reset
        time.sleep(0.05)
        b.write_byte_data(a, self.CTRL3_C, 0x44)        # BDU + auto-increment
        # ODR 416 Hz (0110), FS_XL 01 = +/-16 g  -> 0x64
        b.write_byte_data(a, self.CTRL1_XL, 0x64)
        # ODR 416 Hz (0110), FS_G 11 = +/-2000 dps -> 0x6C
        b.write_byte_data(a, self.CTRL2_G, 0x6C)
        time.sleep(0.1)

    def read(self):
        d = self.bus.read_i2c_block_data(self.addr, self.OUTX_L_G, 12)
        g = [_s16(d[0], d[1]), _s16(d[2], d[3]), _s16(d[4], d[5])]
        a = [_s16(d[6], d[7]), _s16(d[8], d[9]), _s16(d[10], d[11])]
        return self._map(a, g)


class LSM9DS1(_IMUBase):
    """Original Sense HAT. Gyro (0x18) and accel (0x28) are NOT contiguous,
    so it takes two block reads per sample."""
    name = "LSM9DS1 (original Sense HAT)"
    # ST's datasheet lists 0.732 mg/LSB at +/-16 g, which breaks the doubling
    # pattern of the lower ranges (0.061/0.122/0.244). It is widely disputed.
    # The self-test measures gravity at rest and tells you if it's wrong --
    # override with --accel-scale if so.
    default_accel_mg = 0.732        # +/-16 g per datasheet
    gyro_mdps = 70.0                # +/-2000 dps

    CTRL_REG1_G = 0x10
    CTRL_REG4 = 0x1E
    CTRL_REG5_XL = 0x1F
    CTRL_REG6_XL = 0x20
    CTRL_REG8 = 0x22
    OUT_X_L_G = 0x18
    OUT_X_L_XL = 0x28

    def init(self):
        b, a = self.bus, self.addr
        b.write_byte_data(a, self.CTRL_REG8, 0x01)      # software reset
        time.sleep(0.05)
        b.write_byte_data(a, self.CTRL_REG8, 0x44)      # BDU + IF_ADD_INC
        b.write_byte_data(a, self.CTRL_REG4, 0x38)      # enable gyro X/Y/Z
        b.write_byte_data(a, self.CTRL_REG5_XL, 0x38)   # enable accel X/Y/Z
        # ODR_G 101 = 476 Hz, FS_G 11 = +/-2000 dps -> 0xB8
        # (in combined mode the accel runs at the gyro's ODR)
        b.write_byte_data(a, self.CTRL_REG1_G, 0xB8)
        # ODR_XL 101, FS_XL 01 = +/-16 g -> 0xA8
        b.write_byte_data(a, self.CTRL_REG6_XL, 0xA8)
        time.sleep(0.15)
        for _ in range(5):                              # discard settling data
            try:
                self.read()
            except OSError:
                pass
            time.sleep(0.01)

    def read(self):
        dg = self.bus.read_i2c_block_data(self.addr, self.OUT_X_L_G, 6)
        da = self.bus.read_i2c_block_data(self.addr, self.OUT_X_L_XL, 6)
        g = [_s16(dg[0], dg[1]), _s16(dg[2], dg[3]), _s16(dg[4], dg[5])]
        a = [_s16(da[0], da[1]), _s16(da[2], da[3]), _s16(da[4], da[5])]
        return self._map(a, g)


def make_imu(bus, accel_scale=None):
    """Read WHO_AM_I and return the right driver, uninitialised."""
    who = bus.read_byte_data(IMU_ADDR, REG_WHO_AM_I)
    if who == WHOAMI_LSM6DSL:
        return LSM6DSL(bus, accel_mg=accel_scale), who
    if who == WHOAMI_LSM9DS1:
        return LSM9DS1(bus, accel_mg=accel_scale), who
    return None, who


# ============================================================================
#  LED matrix -- direct RGB565 framebuffer
# ============================================================================
FONT_5x7 = {
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
}

# Compact digits so two fit side by side in 8x8 (3 wide + 1 gap + 3 wide).
FONT_3x5 = {
    "0": ["111", "101", "101", "101", "111"],
    "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"],
    "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"],
    "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"],
    "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"],
    "9": ["111", "101", "111", "001", "111"],
}

TICK_8x8 = ["........", "........", ".......#", "......#.",
            ".#...#..", "..#.#...", "...#....", "........"]
CROSS_8x8 = ["........", ".#....#.", "..#..#..", "...##...",
             "...##...", "..#..#..", ".#....#.", "........"]
# "press to start" -- a play triangle
PLAY_8x8 = ["........", ".##.....", ".###....", ".####...",
            ".####...", ".###....", ".##.....", "........"]
# shutdown confirmation -- a power symbol
POWER_8x8 = ["..####..", ".#.##.#.", "#..##..#", "#..##..#",
             "#......#", "#......#", ".#....#.", "..####.."]


class SenseMatrix:
    """8x8 RGB565 framebuffer. Writes are I2C traffic to the HAT's ATTiny on
    the SAME bus as the IMU -- so never draw during a capture."""

    def __init__(self, rotation=0):
        self.rotation = rotation % 360
        self.path = self._find_fb()
        self.enabled = self.path is not None

    @staticmethod
    def _find_fb():
        for name_file in glob.glob("/sys/class/graphics/fb*/name"):
            try:
                with open(name_file) as f:
                    if f.read().strip() == "RPi-Sense FB":
                        return "/dev/" + os.path.basename(
                            os.path.dirname(name_file))
            except OSError:
                continue
        return None

    @staticmethod
    def _rotate(pixels, rotation):
        if rotation == 0:
            return pixels
        grid = [pixels[r * 8:(r + 1) * 8] for r in range(8)]
        for _ in range((rotation // 90) % 4):
            grid = [list(row) for row in zip(*grid[::-1])]
        return [px for row in grid for px in row]

    def show(self, pixels):
        if not self.enabled:
            return
        pixels = self._rotate(pixels, self.rotation)
        packed = bytearray()
        for r, g, b in pixels:
            packed += struct.pack(
                "<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        try:
            with open(self.path, "wb") as f:
                f.write(packed)
        except OSError:
            pass

    def clear(self, colour=(0, 0, 0)):
        self.show([colour] * 64)

    def fill(self, colour):
        self.show([colour] * 64)

    def draw_glyph(self, ch, colour, bg=(0, 0, 0)):
        rows = FONT_5x7.get(str(ch).upper())
        if rows is None:
            self.clear(colour)
            return
        px = [bg] * 64
        for y, row in enumerate(rows):
            for x, bit in enumerate(row):
                if bit == "1":
                    px[(y + 1) * 8 + (x + 1)] = colour   # centre 5x7 in 8x8
        self.show(px)

    def draw_number(self, n, colour, bg=(0, 0, 0)):
        """0-99 as one or two 3x5 digits. 100+ lights the top row as a
        'hundreds' marker and shows the last two digits below it."""
        px = [bg] * 64
        n = max(0, int(n))
        if n >= 100:
            marker = tuple(max(1, c // 3) for c in colour)
            for x in range(8):
                px[x] = marker                          # top row
            n %= 100
        placements = ([(str(n), 2)] if n < 10
                      else [(str(n // 10), 0), (str(n % 10), 4)])
        for ch, x0 in placements:
            for y, row in enumerate(FONT_3x5[ch]):
                for x, bit in enumerate(row):
                    if bit == "1":
                        px[(y + 2) * 8 + (x + x0)] = colour
        self.show(px)

    def draw_bars(self, values, colours, bg=(0, 0, 0)):
        """Up to 4 values as 2-column bars, scaled to the largest, growing up
        from the bottom row. Any non-zero count gets at least one pixel."""
        px = [bg] * 64
        top = max(values) if values else 0
        for i, (v, col) in enumerate(zip(values, colours)):
            if v <= 0 or i > 3:
                continue
            h = max(1, min(8, int(round(v / float(top) * 8)))) if top else 0
            for row in range(8 - h, 8):
                px[row * 8 + i * 2] = col
                px[row * 8 + i * 2 + 1] = col
        self.show(px)

    def draw_pattern(self, pattern, colour, bg=(0, 0, 0)):
        px = [bg] * 64
        for y, row in enumerate(pattern):
            for x, c in enumerate(row):
                if c == "#":
                    px[y * 8 + x] = colour
        self.show(px)

    def flash(self, colour, times=2, on=0.12, off=0.08):
        for _ in range(times):
            self.fill(colour)
            time.sleep(on)
            self.clear()
            time.sleep(off)


# ============================================================================
#  Joystick -- direct evdev
# ============================================================================
EV_KEY = 0x01
KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_ENTER, KEY_DOWN = 103, 105, 106, 28, 108
CODE_TO_DIR = {KEY_UP: "UP", KEY_DOWN: "DOWN", KEY_LEFT: "LEFT",
               KEY_RIGHT: "RIGHT", KEY_ENTER: "MIDDLE"}
DIR_RING = ["UP", "RIGHT", "DOWN", "LEFT"]
EVIOCGRAB = 0x40044590
EVENT_FMT = "llHHi"
EVENT_SIZE = struct.calcsize(EVENT_FMT)


class SenseJoystick:
    """Returns (direction, held_seconds) on release, so a short press and a
    hold can trigger different actions."""

    def __init__(self, rotation=0, grab=True):
        self.rotation = rotation % 360 if JOY_ROTATE_WITH_DISPLAY else 0
        self.path = self._find_device()
        self.fd = None
        self._down = {}
        if self.path:
            try:
                self.fd = os.open(self.path, os.O_RDONLY | os.O_NONBLOCK)
                if grab:
                    try:
                        fcntl.ioctl(self.fd, EVIOCGRAB, 1)
                    except OSError:
                        pass    # not fatal -- it also acts as arrow keys
            except OSError:
                self.fd = None

    @staticmethod
    def _find_device():
        for dev in sorted(glob.glob("/dev/input/event*")):
            n = os.path.basename(dev)
            try:
                with open("/sys/class/input/%s/device/name" % n) as f:
                    if "Sense HAT Joystick" in f.read():
                        return dev
            except OSError:
                continue
        return None

    @property
    def enabled(self):
        return self.fd is not None

    def _map(self, direction):
        if direction == "MIDDLE" or self.rotation == 0:
            return direction
        steps = (self.rotation // 90) % 4
        return DIR_RING[(DIR_RING.index(direction) - steps) % 4]

    def poll(self, timeout=0.2):
        if self.fd is None:
            time.sleep(timeout)
            return None
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return None
        try:
            data = os.read(self.fd, EVENT_SIZE * 32)
        except OSError:
            return None
        for i in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
            _s, _us, etype, code, value = struct.unpack(
                EVENT_FMT, data[i:i + EVENT_SIZE])
            if etype != EV_KEY or code not in CODE_TO_DIR:
                continue
            direction = CODE_TO_DIR[code]
            if value == 1:                              # press
                self._down[direction] = time.monotonic()
            elif value == 0:                            # release
                t0 = self._down.pop(direction, None)
                held = (time.monotonic() - t0) if t0 else 0.0
                return self._map(direction), held
            # value == 2 is autorepeat -- ignored
        return None

    def close(self):
        if self.fd is not None:
            try:
                fcntl.ioctl(self.fd, EVIOCGRAB, 0)
            except OSError:
                pass
            os.close(self.fd)
            self.fd = None


# ============================================================================
#  CSV persistence -- resume, never overwrite, undo by full rewrite
# ============================================================================
def default_name():
    return datetime.now().strftime("cricket_%Y%m%d_%H%M%S.csv")


def home_dir():
    """The real user's home -- even under sudo, so files never land in /root."""
    sudo_user = os.environ.get("SUDO_USER")
    if sudo_user:
        try:
            import pwd
            return pwd.getpwnam(sudo_user).pw_dir
        except (ImportError, KeyError):
            pass
    return os.path.expanduser("~")


def ensure_dir(directory):
    """Resolve, create if missing, and check we can actually write there."""
    directory = os.path.abspath(os.path.expanduser(directory))
    existed = os.path.isdir(directory)
    try:
        os.makedirs(directory, exist_ok=True)
    except OSError as e:
        sys.exit("Cannot create the output folder %s (%s)." % (directory, e))
    if not existed:
        print("Created output folder: %s" % directory)
        # If we're running under sudo, hand the new folder back to the real
        # user so non-sudo runs can still write to it.
        sudo_user = os.environ.get("SUDO_USER")
        if sudo_user:
            try:
                import pwd
                pw = pwd.getpwnam(sudo_user)
                os.chown(directory, pw.pw_uid, pw.pw_gid)
            except (ImportError, KeyError, OSError):
                pass
    if not os.access(directory, os.W_OK):
        sys.exit("No write permission for %s." % directory)
    return directory


def resolve_out(out, directory):
    """A bare filename goes in the session folder. Anything with a directory
    component (./x.csv, /tmp/x.csv, ~/other/x.csv) is respected as given."""
    if out is None:
        return os.path.join(directory, default_name())
    out = os.path.expanduser(out)
    if not out.lower().endswith(".csv"):
        out += ".csv"
    if os.path.dirname(out):
        return os.path.abspath(out)
    return os.path.join(directory, out)


def load_recordings(path):
    """Read a CSV back into memory. Recording ID is only unique WITHIN a shot
    type, so recordings are keyed on (Shot Type, Recording ID) -- keying on the
    ID alone would merge Drive 1 with Pull 1. File order is preserved."""
    recordings = []
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return recordings
    by_key, order = {}, []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                rid = int(row["Recording ID"])
            except (KeyError, ValueError, TypeError):
                continue
            key = (row.get("Shot Type", ""), rid)
            if key not in by_key:
                by_key[key] = {"shot": key[0], "rows": [], "new": False}
                order.append(key)
            by_key[key]["rows"].append([
                row["Timestamp"],
                row["Acceleration X"], row["Acceleration Y"],
                row["Acceleration Z"],
                row["Gyroscope X"], row["Gyroscope Y"], row["Gyroscope Z"],
            ])
    recordings = [by_key[k] for k in order]
    print("Resuming %s: %d recording(s)" % (path, len(recordings)))
    return recordings


def csv_text(recordings):
    """Build the whole file as text: grouped by shot type in SHOTS order, and
    numbered 1..N separately within each group. Numbers are assigned here, at
    export time, so an undo renumbers automatically and never leaves a gap."""
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(HEADERS)
    known = set(SHOTS)
    # SHOTS first, in order; then any other label found in an older file
    groups = SHOTS + sorted({r["shot"] for r in recordings} - known)
    for shot in groups:
        n = 0
        for rec in recordings:
            if rec["shot"] != shot:
                continue
            n += 1
            for s in rec["rows"]:
                w.writerow([n] + s + [shot])
    return buf.getvalue()


def rewrite_csv(path, recordings):
    tmp = path + ".tmp"
    with open(tmp, "w", newline="") as f:
        f.write(csv_text(recordings))
    os.replace(tmp, path)


def backup_if_layout_changed(path, recordings):
    """Resuming a file written by an older version regroups and renumbers it.
    That's the point -- but keep a copy of the original first, just in case.
    Returns the backup path, or None if nothing would change."""
    if not recordings or not os.path.exists(path):
        return None
    try:
        with open(path, newline="") as f:
            current = f.read()
    except OSError:
        return None
    if current == csv_text(recordings):
        return None
    stem, ext = os.path.splitext(path)
    backup = "%s_before_%s%s" % (stem, datetime.now().strftime("%Y%m%d_%H%M%S"),
                                 ext)
    try:
        with open(backup, "w", newline="") as f:
            f.write(current)
    except OSError:
        return None
    return backup


def tally(recordings):
    """Counts for every shot type, including the ones still on zero."""
    counts = dict((s, 0) for s in SHOTS)
    for rec in recordings:
        if rec["shot"] in counts:
            counts[rec["shot"]] += 1
    return counts


# ============================================================================
#  Capture
# ============================================================================
def micros():
    return time.perf_counter_ns() // 1000


def capture_shot(imu, n_samples, rate_hz):
    """Sample n_samples at rate_hz. No LED writes in here -- the matrix shares
    the I2C bus with the IMU and would add jitter."""
    period = 1.0 / rate_hz
    rows = []
    next_t = time.perf_counter()
    for _ in range(n_samples):
        next_t += period
        sample = None
        for _try in range(3):                   # ride out a single I2C glitch
            try:
                sample = imu.read()
                break
            except OSError:
                time.sleep(0.0005)
        if sample is None:
            break
        ax, ay, az, gx, gy, gz = sample
        rows.append([micros(),
                     "%.4f" % ax, "%.4f" % ay, "%.4f" % az,
                     "%.2f" % gx, "%.2f" % gy, "%.2f" % gz])
        dt = next_t - time.perf_counter()
        if dt > 0:
            time.sleep(dt)
        else:
            next_t = time.perf_counter()        # resync, don't burst-catch-up

    if len(rows) < 2:
        return rows, 0.0, 0.0
    ts = [r[0] for r in rows]
    dur = (ts[-1] - ts[0]) / 1e6
    rate = (len(ts) - 1) / dur if dur > 0 else 0.0
    gaps = [(ts[i + 1] - ts[i]) / 1000.0 for i in range(len(ts) - 1)]
    jit = statistics.pstdev(gaps) if len(gaps) > 1 else 0.0
    return rows, rate, jit


def measure_gravity(imu, seconds=2.0):
    """Average |acceleration| while the HAT sits still. Should be 1.000 g."""
    mags, t_end = [], time.monotonic() + seconds
    while time.monotonic() < t_end:
        try:
            ax, ay, az, _, _, _ = imu.read()
            mags.append(math.sqrt(ax * ax + ay * ay + az * az))
        except OSError:
            pass
        time.sleep(0.02)
    return (sum(mags) / len(mags)) if mags else 0.0


# ============================================================================
#  Self-test
# ============================================================================
def selftest(bus, matrix, joy, rate, accel_scale):
    print("\n--- Sense HAT self-test ---------------------------------")

    print("LED matrix : %s" % (matrix.path or "NOT FOUND"))
    if matrix.enabled:
        for col in [(255, 0, 0), (0, 255, 0), (0, 0, 255)]:
            matrix.fill(col)
            time.sleep(0.3)
        for ch in "DPCS":
            matrix.draw_glyph(ch, (255, 255, 255))
            time.sleep(0.3)
        for n in (7, 42, 138):                          # digit rendering
            matrix.draw_number(n, (255, 255, 255))
            time.sleep(0.5)
        matrix.draw_bars([12, 5, 9, 3],
                         [SHOT_COLOUR[s] for s in SHOTS])
        time.sleep(1.0)
        matrix.clear()
        print("             cycled colours, D P C S, the numbers 7 / 42 / 138,")
        print("             then a sample bar chart (12, 5, 9, 3)")
    else:
        print("             no 'RPi-Sense FB' framebuffer -- is the HAT seated,")
        print("             and is the 'sense-hat' package installed?")

    print("Joystick   : %s" % (joy.path or "NOT FOUND"))
    if not joy.enabled:
        print("             no joystick event device. Check the 'input' group:")
        print("             id | grep input")

    try:
        imu, who = make_imu(bus, accel_scale)
    except OSError as e:
        print("IMU        : NOT RESPONDING at 0x%02X (%s)" % (IMU_ADDR, e))
        print("             run  i2cdetect -y 1  -- you should see 6a")
        return
    if imu is None:
        print("IMU        : unknown chip, WHO_AM_I = 0x%02X (expected 0x68 or "
              "0x6A)" % who)
        return
    print("IMU        : %s, WHO_AM_I = 0x%02X  [OK]" % (imu.name, who))
    imu.init()
    print("             configured +/-16 g, +/-2000 dps, "
          "accel scale %.3f mg/LSB" % imu.accel_mg)

    # --- scale sanity check: at rest, |a| must be 1.000 g -------------------
    print("\nKeep the Pi COMPLETELY STILL for 2 seconds...")
    time.sleep(0.8)
    g = measure_gravity(imu)
    print("Gravity    : measured %.3f g at rest (should be ~1.000)" % g)
    if g == 0.0:
        print("             no readings -- something is wrong with the I2C read.")
    elif 0.9 <= g <= 1.1:
        print("             accel scale is correct.  [OK]")
    else:
        print("             SCALE IS OFF by a factor of %.3f." % g)
        print("             Re-run everything with:  --accel-scale %.3f"
              % (imu.accel_mg / g))
        print("             (If the Pi was moving during the check, redo it "
              "first.)")

    # --- live data ---------------------------------------------------------
    print("\nMove the HAT around -- 5 s of live data:")
    t_end = time.monotonic() + 5
    while time.monotonic() < t_end:
        ax, ay, az, gx, gy, gz = imu.read()
        sys.stdout.write("  accel %+7.3f %+7.3f %+7.3f g   "
                         "gyro %+8.1f %+8.1f %+8.1f dps\r"
                         % (ax, ay, az, gx, gy, gz))
        sys.stdout.flush()
        time.sleep(0.1)
    print("\n")

    rows, ach, jit = capture_shot(imu, 100, rate)
    print("Timing     : %d/100 samples at %.1f Hz (target %g), jitter %.2f ms"
          % (len(rows), ach, rate, jit))
    if jit > JIT_MAX:
        print("             jitter is high -- try raising the I2C clock:")
        print("             add  dtparam=i2c_arm_baudrate=400000  to "
              "/boot/firmware/config.txt")

    if joy.enabled:
        print("\nPress each joystick direction (10 s):")
        t_end = time.monotonic() + 10
        while time.monotonic() < t_end:
            ev = joy.poll(0.2)
            if ev:
                print("  %-7s (held %.2f s)" % (ev[0], ev[1]))
    print("--- self-test done --------------------------------------\n")


# ============================================================================
#  Headless launcher -- start, confirm and shut down with no keyboard
# ============================================================================
def wait_for_hardware(bus_num, seconds):
    """Started by systemd at boot, this program can easily beat the Sense HAT
    kernel drivers to the punch. Poll until the framebuffer, the joystick node
    and the I2C bus all exist, rather than failing on a race."""
    deadline = time.monotonic() + seconds
    announced = False
    while True:
        fb = SenseMatrix._find_fb()
        js = SenseJoystick._find_device()
        i2c = os.path.exists("/dev/i2c-%d" % bus_num)
        if fb and js and i2c:
            return True
        if time.monotonic() >= deadline:
            missing = []
            if not fb:
                missing.append("LED matrix framebuffer")
            if not js:
                missing.append("joystick")
            if not i2c:
                missing.append("/dev/i2c-%d" % bus_num)
            print("Timed out waiting for: %s" % ", ".join(missing))
            return False
        if not announced:
            print("Waiting up to %gs for the Sense HAT to come up..." % seconds)
            announced = True
        time.sleep(1.0)


def fail_on_matrix(matrix, C, message):
    """Die loudly on the LED matrix as well as stdout -- on a headless rig the
    red cross is the only error report you'll actually see."""
    print(message)
    if matrix is not None and matrix.enabled:
        for _ in range(3):
            matrix.draw_pattern(CROSS_8x8, C((255, 0, 0)))
            time.sleep(0.35)
            matrix.clear()
            time.sleep(0.15)
        matrix.draw_pattern(CROSS_8x8, C((255, 0, 0)))
        time.sleep(4.0)
        matrix.clear()
    sys.exit(1)


def splash(matrix, C):
    """Confirmation that the program is up: a green wipe, then a tick."""
    if not matrix.enabled:
        return
    col = C((0, 220, 60))
    px = [(0, 0, 0)] * 64
    for row in range(7, -1, -1):
        for x in range(8):
            px[row * 8 + x] = col
        matrix.show(list(px))
        time.sleep(0.045)
    matrix.draw_pattern(TICK_8x8, C((0, 255, 0)))
    time.sleep(0.7)


def confirm_power(matrix, joy, C, timeout=5.0):
    """Red power symbol: press MIDDLE to confirm, anything else cancels."""
    print("Shut down? Press MIDDLE to confirm, any other direction to cancel.")
    matrix.draw_pattern(POWER_8x8, C((255, 0, 0)))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ev = joy.poll(0.15)
        if ev is None:
            continue
        if ev[0] == "MIDDLE":
            return True
        print("cancelled.")
        return False
    print("cancelled (timed out).")
    return False


def do_shutdown(matrix, C):
    """Power the Pi down cleanly so the SD card doesn't get corrupted."""
    print("Shutting down...")
    if matrix.enabled:
        base = C((255, 0, 0))
        for lvl in range(8, -1, -1):
            matrix.draw_pattern(POWER_8x8,
                                tuple(int(c * lvl / 8.0) for c in base))
            time.sleep(0.09)
        matrix.clear()
    for cmd in (["sudo", "-n", "shutdown", "-h", "now"],
                ["systemctl", "poweroff"],
                ["shutdown", "-h", "now"]):
        try:
            r = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10)
            if r.returncode == 0:
                return
        except (OSError, subprocess.SubprocessError):
            continue
    print("Could not trigger a shutdown -- no permission. See the sudoers note")
    print("in the header of this file, or just run:  sudo shutdown -h now")


def wait_for_start(matrix, joy, C):
    """Park on a breathing play symbol until the joystick is pressed.
    Returns 'start' or 'shutdown'."""
    print("\nWaiting. Press the joystick (MIDDLE) to start collecting.")
    print("Hold UP for ~1 s to shut the Pi down.")
    t0 = time.monotonic()
    while True:
        if matrix.enabled:                      # slow breathing pulse
            phase = (math.sin((time.monotonic() - t0) * 2.2) + 1.0) / 2.0
            lvl = 0.25 + 0.75 * phase
            matrix.draw_pattern(
                PLAY_8x8, tuple(int(c * lvl) for c in C((0, 220, 90))))
        ev = joy.poll(0.15)
        if ev is None:
            continue
        direction, held = ev
        if direction == "MIDDLE":
            return "start"
        if direction == "UP" and held >= HOLD_SECONDS:
            if confirm_power(matrix, joy, C):
                return "shutdown"


def launcher_loop(args, imu, matrix, joy, C, out_dir):
    """Press to start a session, hold MIDDLE to end it, press to start the
    next one. The process never exits, so no keyboard is ever needed."""
    while True:
        if wait_for_start(matrix, joy, C) == "shutdown":
            do_shutdown(matrix, C)
            return
        splash(matrix, C)
        if run_session(args, imu, matrix, joy, C, out_dir) == "shutdown":
            do_shutdown(matrix, C)
            return
        if matrix.enabled:                      # session parked, not lost
            matrix.draw_pattern(TICK_8x8, C((0, 120, 255)))
            time.sleep(1.0)


# ============================================================================
#  Main
# ============================================================================
def main():
    ap = argparse.ArgumentParser(
        description="CricketIQ collector for Raspberry Pi + Sense HAT")
    ap.add_argument("--out", default=None,
                    help="CSV filename to write/resume (goes in the folder "
                         "below unless you give a path)")
    ap.add_argument("--dir", default=None, dest="outdir",
                    help="folder for the CSVs (default: ~/%s)" % OUT_DIR_NAME)
    ap.add_argument("--bus", type=int, default=1, help="I2C bus (default 1)")
    ap.add_argument("--samples", type=int, default=50,
                    help="samples per shot (default 50)")
    ap.add_argument("--rate", type=float, default=25.0,
                    help="sample rate, Hz (default 25 -- 50 samples = a 2 s "
                         "window)")
    ap.add_argument("--delay", type=float, default=3.0,
                    help="countdown seconds between the press and the capture "
                         "(default 3)")
    ap.add_argument("--rotate", type=int, default=0, choices=[0, 90, 180, 270],
                    help="rotate the LED matrix (and joystick) orientation")
    ap.add_argument("--brightness", type=float, default=0.4,
                    help="LED brightness 0.05-1.0 (default 0.4)")
    ap.add_argument("--idle", default="cycle",
                    choices=["cycle", "letter", "count", "bars"],
                    help="what the matrix shows between shots (default cycle)")
    ap.add_argument("--accel-scale", type=float, default=None,
                    help="accel mg/LSB override (see the self-test gravity check)")
    ap.add_argument("--no-grab", action="store_true",
                    help="don't grab the joystick (it also acts as arrow keys)")
    ap.add_argument("--wait-for-press", action="store_true",
                    help="park on a 'press to start' screen instead of "
                         "collecting immediately; used by the systemd service")
    ap.add_argument("--wait-hardware", type=float, default=30.0,
                    help="seconds to wait for the Sense HAT at boot (default 30)")
    ap.add_argument("--allow-shutdown", action="store_true",
                    help="enable hold-UP to power the Pi down (always on with "
                         "--wait-for-press)")
    ap.add_argument("--selftest", action="store_true",
                    help="check the HAT end to end, then exit")
    args = ap.parse_args()

    dim = max(0.05, min(1.0, args.brightness))

    def C(rgb):
        return tuple(int(c * dim) for c in rgb)

    if args.wait_hardware > 0:
        wait_for_hardware(args.bus, args.wait_hardware)

    try:
        bus = SMBus(args.bus)
    except (FileNotFoundError, PermissionError) as e:
        fail_on_matrix(SenseMatrix(rotation=args.rotate), C,
                       "Cannot open I2C bus %d (%s). Enable I2C and check "
                       "you're in the 'i2c' group." % (args.bus, e))

    matrix = SenseMatrix(rotation=args.rotate)
    joy = SenseJoystick(rotation=args.rotate, grab=not args.no_grab)

    if args.selftest:
        selftest(bus, matrix, joy, args.rate, args.accel_scale)
        joy.close()
        bus.close()
        return

    # ---- bring up the IMU -------------------------------------------------
    try:
        imu, who = make_imu(bus, args.accel_scale)
    except OSError as e:
        fail_on_matrix(matrix, C,
                       "No IMU at 0x%02X (%s). Run  i2cdetect -y %d  -- you "
                       "should see 6a. Is the HAT seated?"
                       % (IMU_ADDR, e, args.bus))
    if imu is None:
        fail_on_matrix(matrix, C,
                       "Unknown IMU: WHO_AM_I = 0x%02X (expected 0x68 LSM9DS1 "
                       "or 0x6A LSM6DSL)." % who)
    imu.init()
    print("IMU ready: %s, +/-16 g / +/-2000 dps, accel scale %.3f mg/LSB"
          % (imu.name, imu.accel_mg))

    if not matrix.enabled:
        print("Warning: LED matrix not found -- running without visual feedback.")
    if not joy.enabled:
        fail_on_matrix(matrix, C,
                       "Joystick not found. Check the HAT is seated and that "
                       "you're in the 'input' group (id | grep input).")

    out_dir = ensure_dir(args.outdir or os.path.join(home_dir(), OUT_DIR_NAME))

    try:
        if args.wait_for_press:
            launcher_loop(args, imu, matrix, joy, C, out_dir)
        else:
            splash(matrix, C)
            if run_session(args, imu, matrix, joy, C, out_dir) == "shutdown":
                do_shutdown(matrix, C)
    except KeyboardInterrupt:
        pass
    finally:
        matrix.clear()
        joy.close()
        try:
            bus.close()
        except Exception:
            pass


def run_session(args, imu, matrix, joy, C, out_dir):
    """One collection session: open/resume the CSV, then run the joystick loop
    until the user holds MIDDLE. Returns 'quit' or 'shutdown'."""
    # ---- session state ----------------------------------------------------
    path = resolve_out(args.out, out_dir)
    print("Saving to: %s" % path)
    recordings = load_recordings(path)
    backup = backup_if_layout_changed(path, recordings)
    if backup:
        print("Regrouping by shot type and renumbering 1..N per type.")
        print("Original saved as: %s" % backup)
    rewrite_csv(path, recordings)
    state = {"samples": max(2, args.samples), "shot": SHOTS[0]}

    # ---- display helpers --------------------------------------------------
    FRAMES = {"cycle": ["letter", "count", "bars"], "letter": ["letter"],
              "count": ["count"], "bars": ["bars"]}[args.idle]
    idle = {"phase": 0, "next_flip": 0.0}

    def counts():
        return tally(recordings)

    def draw_frame(kind):
        s = state["shot"]
        col = C(SHOT_COLOUR[s])
        if kind == "letter":
            matrix.draw_glyph(SHOT_GLYPH[s], col)
        elif kind == "count":
            matrix.draw_number(counts()[s], col)
        else:
            c = counts()
            matrix.draw_bars([c[x] for x in SHOTS],
                             [C(SHOT_COLOUR[x]) for x in SHOTS])

    def reset_idle():
        """Snap back to the first frame -- used after any action, so you always
        see the armed shot type immediately."""
        idle["phase"] = 0
        idle["next_flip"] = time.monotonic() + IDLE_FLIP_SECONDS
        draw_frame(FRAMES[0])

    def tick_idle():
        """Called from the main loop while nothing else is happening."""
        if len(FRAMES) == 1:
            return
        now = time.monotonic()
        if now < idle["next_flip"]:
            return
        idle["phase"] = (idle["phase"] + 1) % len(FRAMES)
        idle["next_flip"] = now + IDLE_FLIP_SECONDS
        draw_frame(FRAMES[idle["phase"]])

    def status():
        c = counts()
        print("[%s | %s | %d samp @ %g Hz | %d rec | %s]"
              % (os.path.basename(path), state["shot"], state["samples"],
                 args.rate, len(recordings),
                 "  ".join("%s %d" % (SHOT_GLYPH[s], c[s]) for s in SHOTS)))

    # ---- actions ----------------------------------------------------------
    def do_record():
        shot = state["shot"]
        n = state["samples"]
        sys.stdout.write("* %s: countdown... " % shot)
        sys.stdout.flush()
        # countdown BEFORE capture -- LED writes are I2C traffic, keep them
        # out of the sampling window
        steps = max(1, int(round(args.delay)))
        for i in range(steps, 0, -1):
            matrix.draw_glyph(str(min(i, 9)), C((255, 255, 255)))
            time.sleep(args.delay / steps)
        matrix.fill(C((120, 0, 0)))             # one write, then hands off
        time.sleep(0.02)

        rows, ach, jit = capture_shot(imu, n, args.rate)

        if len(rows) < n:
            print("only %d/%d samples -- I2C stalled. NOT saved." % (len(rows), n))
            matrix.draw_pattern(CROSS_8x8, C((255, 0, 0)))
            time.sleep(0.8)
            reset_idle()
            return
        lo, hi = args.rate * (1 - RATE_TOL), args.rate * (1 + RATE_TOL)
        ok = (lo <= ach <= hi) and (jit <= JIT_MAX)
        rec = {"shot": shot, "rows": rows, "new": True}
        recordings.append(rec)
        rewrite_csv(path, recordings)
        c = counts()
        print("%s #%d | %d samp | %.1f Hz | jit %.2f ms %s  (%d total)"
              % (shot, c[shot], len(rows), ach, jit,
                 "[OK]" if ok else "[CHECK - rate/jitter off]",
                 len(recordings)))
        matrix.draw_pattern(TICK_8x8 if ok else CROSS_8x8,
                            C((0, 255, 0) if ok else (255, 140, 0)))
        time.sleep(0.6)
        matrix.draw_number(c[shot], C(SHOT_COLOUR[shot]))   # new count
        time.sleep(0.9)
        reset_idle()

    def do_undo():
        if not recordings:
            print("nothing to undo.")
            matrix.flash(C((255, 140, 0)), times=1)
            reset_idle()
            return
        # Prefer the last shot recorded in THIS session. After resuming a file
        # the in-memory order is grouped by type, not chronological, so the
        # plain last entry could be someone else's Stationary from last week.
        idx = next((i for i in range(len(recordings) - 1, -1, -1)
                    if recordings[i].get("new")), len(recordings) - 1)
        gone = recordings[idx]
        was = counts()[gone["shot"]]            # its per-type number
        if not gone.get("new"):
            print("(nothing recorded this session - removing from the "
                  "existing file)")
        recordings.pop(idx)
        rewrite_csv(path, recordings)
        c = counts()
        print("undid %s #%d. %s now %d, %d total."
              % (gone["shot"], was, gone["shot"], c.get(gone["shot"], 0),
                 len(recordings)))
        matrix.draw_pattern(CROSS_8x8, C((255, 140, 0)))
        time.sleep(0.5)
        if gone["shot"] in c:
            matrix.draw_number(c[gone["shot"]], C(SHOT_COLOUR[gone["shot"]]))
            time.sleep(0.8)
        reset_idle()

    def do_show_tally():
        c = counts()
        print("tally: " + "  ".join("%s %d" % (s, c[s]) for s in SHOTS)
              + "   (total %d)" % len(recordings))
        matrix.draw_bars([c[s] for s in SHOTS],
                         [C(SHOT_COLOUR[s]) for s in SHOTS])
        time.sleep(3.0)
        reset_idle()

    def do_new_file():
        nonlocal path, recordings
        path = os.path.join(out_dir, default_name())
        recordings = []
        rewrite_csv(path, recordings)
        print("\nNew file: %s" % path)
        matrix.flash(C((0, 200, 255)), times=2)
        reset_idle()
        status()

    def do_show_window():
        """Read-only. The window is fixed now, so this just confirms it rather
        than cycling the sample count and silently breaking the dataset."""
        n = state["samples"]
        print("window = %d samples @ %g Hz (%.1f s), countdown %.1f s"
              % (n, args.rate, n / args.rate, args.delay))
        matrix.draw_number(n, C((255, 255, 255)))
        time.sleep(0.9)
        reset_idle()

    def do_cycle_shot(step):
        i = (SHOTS.index(state["shot"]) + step) % len(SHOTS)
        state["shot"] = SHOTS[i]
        print("shot type = %s  (%d recorded)"
              % (state["shot"], counts()[state["shot"]]))
        reset_idle()

    print("\nReady. UP/DOWN shot type | MIDDLE record | hold MIDDLE stop | "
          "LEFT undo | hold LEFT tally | RIGHT window info | hold RIGHT new file")
    print("       %d samples @ %g Hz = %.1f s window, %.1f s countdown"
          % (state["samples"], args.rate, state["samples"] / args.rate,
             args.delay))
    can_shutdown = args.wait_for_press or args.allow_shutdown
    if can_shutdown:
        print("       hold UP to shut the Pi down safely.")
    print("")
    status()
    reset_idle()

    reason = "quit"
    try:
        while True:
            ev = joy.poll(0.2)
            if ev is None:
                tick_idle()
                continue
            direction, held = ev
            hold = held >= HOLD_SECONDS
            if direction == "UP":
                if hold and can_shutdown:
                    if confirm_power(matrix, joy, C):
                        reason = "shutdown"
                        break
                    reset_idle()
                else:
                    do_cycle_shot(-1)
            elif direction == "DOWN":
                do_cycle_shot(+1)
            elif direction == "MIDDLE":
                if hold:
                    break
                do_record()
            elif direction == "LEFT":
                if hold:
                    do_show_tally()
                else:
                    do_undo()
            elif direction == "RIGHT":
                if hold:
                    do_new_file()
                else:
                    do_show_window()
    finally:
        c = tally(recordings)
        print("\nSession done. %d recording(s) saved to %s"
              % (len(recordings), path))
        print("Counts: " + "  ".join("%s %d" % (s, c[s]) for s in SHOTS))
        if matrix.enabled and recordings:
            matrix.draw_bars([c[s] for s in SHOTS],
                             [C(SHOT_COLOUR[s]) for s in SHOTS])
            time.sleep(2.0)
    return reason


if __name__ == "__main__":
    main()
