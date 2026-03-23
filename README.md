# Holy Stone H120D — Reverse Engineering

Complete protocol documentation and control tools for the **Holy Stone H120D** GPS drone, reverse-engineered from the stock Android app (`hs_gps.apk`) and live packet captures.

This is the first public documentation of the H120D's WiFi protocol. The drone uses a **Fullhan Micro SoC** running **RT-Thread 2.1.0 RTOS** — not the Linux/HiSilicon stack common in other Holy Stone models.

## What's Here

| Path | Description |
|------|-------------|
| [`PROTOCOL.md`](PROTOCOL.md) | Full protocol reference — packet formats, handshake, commands |
| [`docs/RTOS_INTERNALS.md`](docs/RTOS_INTERNALS.md) | RT-Thread shell findings — threads, devices, filesystem |
| [`docs/NATIVE_LIB.md`](docs/NATIVE_LIB.md) | ARM disassembly of `convertHyControl()` from `libLGDataUtils.so` |
| [`examples/h120d_control.py`](examples/h120d_control.py) | Standalone Python controller — handshake, flight, video |
| [`examples/hisi_camera.py`](examples/hisi_camera.py) | HiSi camera commands (UDP 8088) — photo, video, settings |
| [`examples/live_video.py`](examples/live_video.py) | Live H.264 video viewer (TCP 8888 → ffplay) |
| [`arduino/h120d_controller.ino`](arduino/h120d_controller.ino) | Arduino Nano 33 IoT autonomous controller sketch |

## Quick Start

### Prerequisites
- Python 3.8+ with `socket` (stdlib only — no pip installs needed)
- Connect your PC/device to the drone's WiFi: **`HolyStoneFPV-XXXXXX`** (open network)
- Drone IP is always `172.16.10.1`

### Talk to the drone in 10 lines

```python
import socket, time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
DRONE = ("172.16.10.1", 8080)

sock.sendto(b'\x0f', DRONE)                          # Query resolution
print(sock.recvfrom(256))                             # → "1080P"

sock.sendto(b'\x28\x42\x47\x2c', DRONE)              # Version handshake
print(sock.recvfrom(256))                             # → "V3.8.2"

# Heartbeat (required to stay connected)
local_ip = sock.getsockname()  # after first sendto
sock.sendto(bytes([0x09, 172, 16, 130, 1]), DRONE)    # → "ok"
```

### Watch the video stream

```bash
# Requires ffplay (from ffmpeg)
python examples/live_video.py
```

### Full autonomous flight (Arduino)

Flash `arduino/h120d_controller.ino` to an Arduino Nano 33 IoT, open serial at 115200, type `AUTO`.

## Protocol Overview

| Channel | Port | Protocol | Purpose |
|---------|------|----------|---------|
| Command/Control | **UDP 8080** | Custom binary | Handshake, heartbeat, GPS, flight commands |
| Video Stream | **TCP 8888** | H.264 + custom headers | 1280x720 25fps live video |
| Camera Control | **UDP 8088** | HiSi binary | Photo, video recording, sensor queries |
| Telnet Shell | **TCP 23** | RT-Thread finsh | RTOS shell access (triggers MAC ban!) |
| Unknown | **TCP 8830** | ? | Discovered via `list_tcps()`, unexplored |

### Packet Formats (UDP 8080)

**GPS/Status (19 bytes, 5Hz)** — Phone sends its GPS to the drone for RTH/FollowMe:
```
5A 55 0F 01 [lat:4B] [lon:4B] [accuracy:2B] [orientation:2B] [follow:2B] [XOR]
```

**Flight Control (12 bytes, 50Hz burst)** — Takeoff, land, joystick axes:
```
5A 55 08 02 [flags] [axis1] [axis2] [axis3] [axis4] [trim1] [trim2] [XOR]
```

**Heartbeat (5 bytes, 1Hz)** — Required to stay connected:
```
09 [IP_byte1] [IP_byte2] [IP_byte3] [IP_byte4]
```

See [`PROTOCOL.md`](PROTOCOL.md) for complete details.

## Key Discoveries

1. **Two independent control paths** — Physical RC uses 2.4GHz radio directly to the flight controller. WiFi goes through the Fullhan SoC. They don't share state.

2. **GPS accuracy gate** — The drone won't accept takeoff until the GPS status packet reports accuracy ≤ 3 meters. The "state byte" values (7→6→5→4→3) are literal accuracy in meters, not a state machine.

3. **No joystick over WiFi** — The stock app's `convertHyControl()` JNI function exists in the native library but is **never called from Java**. The app only sends flags (takeoff/land/goHome) with axes hardcoded to center. Variable axis control is possible but untested at high confidence.

4. **Video is TCP, not RTSP** — Despite port 554 existing, the actual video path is TCP 8888 with an 11-byte heartbeat and custom 44-byte frame headers wrapping H.264 NAL units.

5. **Telnet MAC ban** — Connecting to port 23 triggers a MAC-level ban on the connecting device. Power cycle clears it. Using a second device (like an Arduino) as a bridge bypasses this.

6. **Fullhan, not HiSilicon** — The SoC is a Fullhan Micro chip (FH8620/FH8830 family), identified by `fh_` device prefixes in RT-Thread. ARM9/ARM11 core, not Cortex-M.

## Related Projects

- [benjamind2/HS720](https://github.com/benjamind2/HS720) — Holy Stone HS720 reverse engineering
- [hybridgroup/gobot HS200](https://github.com/hybridgroup/gobot/tree/master/platforms/holystone/hs200) — Go driver for HS200
- [lancecaraccioli/holystone-hs110w](https://github.com/lancecaraccioli/holystone-hs110w) — HS110W web interface

The H120D uses the **Heliway "Hy" variant** of the `com.vison.macrochip` protocol family — same app, same ports, but different wire format than the S2x drones documented by TurboDrone.

## Methodology

All findings derived from:
- **APK decompilation** via jadx (stock app `com.vison.macrochip.sj.hs.gps.v1`)
- **Packet capture** via tcpdump on a rooted Android tablet connected to the drone
- **ARM disassembly** of `libLGDataUtils.so` (native JNI library)
- **Live testing** with Python scripts, Arduino Nano 33 IoT, and ALFA AWUS036NHA WiFi adapter
- **RT-Thread shell exploration** via telnet port 23

## License

This is independent security research for educational purposes. Holy Stone is a trademark of Holy Stone Enterprise Co., Ltd. This project is not affiliated with or endorsed by Holy Stone.
