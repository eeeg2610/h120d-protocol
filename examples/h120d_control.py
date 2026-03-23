#!/usr/bin/env python3
"""
H120D Drone Controller — Standalone Python
============================================
Complete control script for the Holy Stone H120D drone over WiFi.
No external dependencies — stdlib only.

Usage:
    python h120d_control.py handshake     # Run handshake sequence
    python h120d_control.py heartbeat     # Run heartbeat (Ctrl+C to stop)
    python h120d_control.py gps           # GPS + heartbeat stream (Ctrl+C to stop)
    python h120d_control.py takeoff       # Send takeoff burst
    python h120d_control.py land          # Send land burst
    python h120d_control.py gohome        # Send return-to-home burst
    python h120d_control.py estop         # Emergency stop
    python h120d_control.py auto          # Full autonomous takeoff sequence

Prerequisites:
    - Connect to the drone's WiFi network (HolyStoneFPV-XXXXXX, open)
    - Drone IP is always 172.16.10.1
    - RC must arm motors before WiFi takeoff works
"""
import socket
import struct
import sys
import time
import datetime

# ============================================================
# CONFIGURATION
# ============================================================
DRONE_IP = "172.16.10.1"
CMD_PORT = 8080

# GPS coordinates sent to drone for RTH/FollowMe (set to your location)
# Format: degrees * 10^7 as int32
GPS_LAT = 0           # Example: 40.7128° N → 407128000
GPS_LON = 0           # Example: -74.0060° W → -740060000
GPS_ACCURACY = 3      # meters (drone requires ≤ 3 for takeoff)
GPS_ORIENTATION = 0   # compass heading in degrees

# Flight control constants
FLAG_TAKEOFF = 0x01
FLAG_LAND    = 0x02
FLAG_GOHOME  = 0x04
FLAG_ESTOP   = 0x80

# Joystick center values (from native library disassembly)
AXIS_CENTER = (0x7F, 0x7F, 0x80, 0x80)  # pitch, roll, throttle, yaw
TRIM_DEFAULT = 0x20

# Timing (from stock app packet captures)
HEARTBEAT_INTERVAL = 1.0   # seconds
GPS_INTERVAL       = 0.2   # seconds (5Hz)
CONTROL_INTERVAL   = 0.02  # seconds (50Hz)
BURST_COUNT        = 50    # packets per command burst
PRE_FLIGHT_SECS    = 5     # GPS stream before takeoff

# ============================================================
# PACKET BUILDERS
# ============================================================

def xor_checksum(data, start, end):
    """XOR all bytes in range [start, end)."""
    r = 0
    for i in range(start, end):
        r ^= data[i]
    return r


def build_heartbeat(local_ip):
    """5-byte heartbeat: 0x09 + local IP address bytes."""
    return bytes([0x09]) + socket.inet_aton(local_ip)


def build_time_sync():
    """29-byte time sync: 0x26 + 7× uint32 LE (year, month, day, weekday, hour, min, sec)."""
    now = datetime.datetime.now()
    weekday = now.isoweekday() % 7  # 0=Sun
    return struct.pack('<B7I', 0x26,
                       now.year, now.month, now.day, weekday,
                       now.hour, now.minute, now.second)


def build_gps_status(lat=GPS_LAT, lon=GPS_LON, accuracy=GPS_ACCURACY, orientation=GPS_ORIENTATION, follow=False):
    """19-byte GPS/status packet — sends phone position to drone for RTH/FollowMe."""
    pkt = bytearray(19)
    pkt[0:4] = b'\x5A\x55\x0F\x01'
    struct.pack_into('>i', pkt, 4, lat)
    struct.pack_into('>i', pkt, 8, lon)
    struct.pack_into('>H', pkt, 12, accuracy)
    struct.pack_into('>h', pkt, 14, orientation)
    pkt[16:18] = b'\xFF\xFF' if follow else b'\x00\x00'
    pkt[18] = xor_checksum(pkt, 2, 18)
    return bytes(pkt)


def build_flight_control(flags=0, axes=AXIS_CENTER, trims=(TRIM_DEFAULT, TRIM_DEFAULT)):
    """12-byte flight control packet."""
    pkt = bytearray([
        0x5A, 0x55, 0x08, 0x02,
        flags & 0xFF,
        axes[0] & 0xFF, axes[1] & 0xFF, axes[2] & 0xFF, axes[3] & 0xFF,
        trims[0], trims[1],
        0x00  # checksum placeholder
    ])
    pkt[11] = xor_checksum(pkt, 2, 11)
    return bytes(pkt)


# ============================================================
# DRONE COMMUNICATION
# ============================================================

class H120DController:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.5)
        self.tx = 0
        self.local_ip = None

    def send(self, data):
        self.sock.sendto(data, (DRONE_IP, CMD_PORT))
        self.tx += 1
        # Capture local IP on first send
        if self.local_ip is None:
            self.local_ip = self.sock.getsockname()[0]

    def recv(self, quiet=False):
        try:
            data, addr = self.sock.recvfrom(256)
            if not quiet:
                try:
                    text = data.decode('ascii').strip()
                    if all(0x20 <= b < 0x7F or b in (0x0A, 0x0D, 0x00, 0x01) for b in data):
                        print(f'  [DRONE] "{text}"')
                    else:
                        print(f"  [DRONE] HEX[{len(data)}] {data.hex(' ')}")
                except UnicodeDecodeError:
                    print(f"  [DRONE] HEX[{len(data)}] {data.hex(' ')}")
            return data
        except socket.timeout:
            return None

    def send_cmd(self, byte_val):
        self.send(bytes([byte_val]))

    def handshake(self):
        """Full handshake sequence (from stock app capture)."""
        print("[HANDSHAKE] Starting...")

        # 1. Resolution query
        self.send_cmd(0x0F)
        time.sleep(0.05); self.recv()

        # 2. Version handshake
        for c in [0x28, 0x42, 0x47, 0x2C]:
            self.send_cmd(c)
        time.sleep(0.05); self.recv(); self.recv(); self.recv()

        # 3. Time sync
        self.send(build_time_sync())
        time.sleep(0.05); self.recv()

        # 4. Keyframe request
        self.send_cmd(0x27)
        time.sleep(0.05); self.recv()

        # 5. Second version query (stock app repeats this)
        time.sleep(0.3)
        for c in [0x28, 0x42, 0x47, 0x2C]:
            self.send_cmd(c)
        time.sleep(0.05); self.recv(); self.recv()

        print("[HANDSHAKE] Complete.")

    def heartbeat(self):
        """Send one heartbeat packet."""
        if self.local_ip:
            self.send(build_heartbeat(self.local_ip))

    def gps(self, accuracy=GPS_ACCURACY):
        """Send one GPS status packet."""
        self.send(build_gps_status(accuracy=accuracy))

    def command_burst(self, flags, count, name):
        """Send a burst of flight control packets (mimics stock app pattern)."""
        print(f"[FLIGHT] Sending {name} burst ({count} packets)...")
        for i in range(count):
            self.send(build_flight_control(flags))
            if i % 5 == 0:
                self.gps()
            time.sleep(CONTROL_INTERVAL)
            self.recv(quiet=True)

        # Follow with idle packets (stock app does this)
        for i in range(20):
            self.send(build_flight_control(0x00))
            time.sleep(CONTROL_INTERVAL)

        print(f"[FLIGHT] {name} complete.")

    def run_streams(self, duration):
        """Run heartbeat + GPS stream for given seconds."""
        start = time.time()
        last_hb = 0
        last_gps = 0
        while time.time() - start < duration:
            now = time.time()
            if now - last_hb >= HEARTBEAT_INTERVAL:
                self.heartbeat()
                last_hb = now
            if now - last_gps >= GPS_INTERVAL:
                self.gps()
                last_gps = now
            self.recv(quiet=True)
            time.sleep(0.01)

    def auto_takeoff(self):
        """Full autonomous sequence: handshake → GPS stream → takeoff → hover."""
        print("=" * 50)
        print("  AUTONOMOUS TAKEOFF SEQUENCE")
        print("  Make sure RC has armed the motors first!")
        print("=" * 50)

        print("\n[AUTO] Phase 1: Handshake...")
        self.handshake()
        time.sleep(0.2)

        print(f"[AUTO] Phase 2: GPS stream ({PRE_FLIGHT_SECS}s)...")
        self.run_streams(PRE_FLIGHT_SECS)

        print("[AUTO] Phase 3: TAKEOFF!")
        self.command_burst(FLAG_TAKEOFF, BURST_COUNT, "TAKEOFF")

        print("[AUTO] Phase 4: Hovering — maintaining GPS + heartbeat")
        print("[AUTO] Press Ctrl+C to stop streams, then run: land")
        try:
            while True:
                self.run_streams(1)
        except KeyboardInterrupt:
            print("\n[AUTO] Streams stopped.")

    def close(self):
        self.sock.close()
        print(f"[TX: {self.tx} packets]")


# ============================================================
# MAIN
# ============================================================

COMMANDS = {
    "handshake": "Run handshake sequence",
    "heartbeat": "Run heartbeat (Ctrl+C to stop)",
    "gps":       "GPS + heartbeat stream (Ctrl+C to stop)",
    "takeoff":   "Send takeoff burst",
    "land":      "Send land burst",
    "gohome":    "Send return-to-home burst",
    "estop":     "Emergency stop",
    "auto":      "Full autonomous takeoff sequence",
}

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        print("H120D Drone Controller")
        print(f"Usage: python {sys.argv[0]} <command>\n")
        for cmd, desc in COMMANDS.items():
            print(f"  {cmd:12s} {desc}")
        print(f"\nDrone IP: {DRONE_IP}:{CMD_PORT}")
        print("Connect to the drone's WiFi first (HolyStoneFPV-XXXXXX)")
        sys.exit(1)

    cmd = sys.argv[1].lower()
    ctrl = H120DController()

    try:
        if cmd == "handshake":
            ctrl.handshake()

        elif cmd == "heartbeat":
            ctrl.handshake()
            print("Heartbeat running (Ctrl+C to stop)...")
            try:
                while True:
                    ctrl.heartbeat()
                    ctrl.recv()
                    time.sleep(HEARTBEAT_INTERVAL)
            except KeyboardInterrupt:
                print("\nStopped.")

        elif cmd == "gps":
            ctrl.handshake()
            print("GPS + heartbeat running (Ctrl+C to stop)...")
            try:
                ctrl.run_streams(999999)
            except KeyboardInterrupt:
                print("\nStopped.")

        elif cmd == "takeoff":
            ctrl.command_burst(FLAG_TAKEOFF, BURST_COUNT, "TAKEOFF")

        elif cmd == "land":
            ctrl.command_burst(FLAG_LAND, BURST_COUNT, "LAND")

        elif cmd == "gohome":
            ctrl.command_burst(FLAG_GOHOME, BURST_COUNT, "GOHOME")

        elif cmd == "estop":
            ctrl.command_burst(FLAG_ESTOP, 100, "ESTOP")
            print("*** ALL STOPPED ***")

        elif cmd == "auto":
            ctrl.auto_takeoff()

    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        ctrl.close()


if __name__ == "__main__":
    main()
