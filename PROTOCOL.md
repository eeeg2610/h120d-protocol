# H120D Protocol Reference

Complete WiFi protocol documentation for the Holy Stone H120D GPS drone.
All data verified through packet captures and live testing.

---

## 1. Hardware Summary

| Parameter | Value |
|-----------|-------|
| Drone WiFi SSID | `HolyStoneFPV-XXXXXX` (open network, last 6 = MAC suffix) |
| Drone IP | `172.16.10.1` |
| Subnet | `172.16.0.0/16` (DHCP) |
| Firmware | V3.8.2 |
| SoC | **Fullhan Micro** (FH8620/FH8830 family, ARM9/ARM11) |
| RTOS | **RT-Thread 2.1.0** (built Oct 1, 2019) |
| Shell | finsh (C-expression interpreter, port 23) |
| Stock app | `com.vison.macrochip.sj.hs.gps.v1` (Android) |
| Protocol family | Heliway "Hy" variant of the Vison/Macrochip protocol |

## 2. Network Ports

| Port | Protocol | Purpose | Notes |
|------|----------|---------|-------|
| **8080** | UDP | Command & control | Handshake, heartbeat, GPS, flight commands |
| **8888** | TCP | Video stream | H.264 1280x720 25fps, custom frame headers |
| **8088** | UDP | Camera/sensor commands | HiSi protocol, stateless, no handshake |
| **23** | TCP | Telnet (RT-Thread finsh) | **Caution: triggers MAC ban on connecting device** |
| **8830** | TCP | Unknown | Discovered listening, unexplored |
| 554 | TCP | RTSP (non-functional) | Port exists but delivers no frames |
| 80 | TCP | HTTP (refused) | RST on connect |

## 3. Connection Handshake (UDP 8080)

The stock app performs this exact sequence on startup:

| Step | Send | Receive | Purpose |
|------|------|---------|---------|
| 1 | `0x0F` (1 byte) | `"1080P"` | Query video resolution |
| 2 | `0x28 0x42 0x47 0x2C` ("(BG,") | `"V3.8.2"` | Query firmware version |
| 3 | 29-byte time sync (see below) | `"timeok"` | Sync clock |
| 4 | `0x27` (1 byte) | `"forceI"` | Request video keyframe |
| 5 | `0x28 0x42 0x47 0x2C` (repeat) | `"V3.8.2"` + `"ok1"` | Confirm handshake |
| 6 | — | `"drone0\n"` | Drone sends device ID |

### Time Sync Packet (29 bytes)
```
Byte 0:    0x26 (command ID)
Bytes 1-4: Year   (uint32, little-endian)
Bytes 5-8: Month  (uint32, little-endian)
Bytes 9-12: Day   (uint32, little-endian)
Bytes 13-16: Weekday (uint32, LE) — 0=Sun, 1=Mon, ...
Bytes 17-20: Hour  (uint32, little-endian)
Bytes 21-24: Minute (uint32, little-endian)
Bytes 25-28: Second (uint32, little-endian)
```

Example (2026-03-18 20:54:48 Wednesday):
```
26 EA 07 00 00 03 00 00 00 12 00 00 00 03 00 00 00 14 00 00 00 36 00 00 00 30 00 00 00
```

### Heartbeat (5 bytes, send every 1 second)
```
09 [IP_B1] [IP_B2] [IP_B3] [IP_B4]
```
Where `IP_B1-B4` are your local IP address bytes. Drone responds with `"ok "` at 1Hz.
**The drone will disconnect you if heartbeat stops.**

## 4. GPS/Status Packet (19 bytes, 5Hz)

Sent continuously from client to drone on UDP 8080. Contains the **phone's GPS position** (not the drone's) — used for Return-to-Home and Follow Me.

```
Offset:  0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16   17   18
       [5A] [55] [0F] [01] [------LAT------] [------LON------] [--ACC--] [--ORI--] [--FOL--] [XOR]
```

| Bytes | Type | Description |
|-------|------|-------------|
| 0-1 | — | Frame header: `5A 55` |
| 2-3 | — | Packet type: `0F 01` |
| 4-7 | int32 BE | Latitude (degrees × 10^7) |
| 8-11 | int32 BE | Longitude (degrees × 10^7) |
| 12-13 | uint16 BE | **GPS accuracy in meters** — drone requires ≤ 3 for takeoff |
| 14-15 | int16 BE | Compass orientation (degrees) |
| 16-17 | uint16 | Follow mode: `FF FF` = following, `00 00` = normal |
| 18 | uint8 | XOR checksum of bytes 2-17 |

### GPS Accuracy Gate

The drone will not accept flight commands until it receives GPS packets with accuracy ≤ 3 meters. The stock app starts sending high values (7, 6, 5...) that decrease as the phone's GPS improves. The values 7→6→5→4→3 are **literal meters**, not state IDs.

Once accuracy reaches 3, the drone is ready. Continue sending accuracy=3 during flight.

## 5. Flight Control Packet (12 bytes, 50Hz burst)

Sent in rapid bursts when a flight command is triggered. Typical burst: 50-75 packets at 20ms intervals, followed by 20 idle packets.

```
Offset:  0    1    2    3    4    5    6    7    8    9   10   11
       [5A] [55] [08] [02] [FL] [A1] [A2] [A3] [A4] [T1] [T2] [XOR]
```

| Byte | Description | Default |
|------|-------------|---------|
| 0-1 | Frame header: `5A 55` | — |
| 2-3 | Packet type: `08 02` | — |
| 4 | **Flags** (bitfield) | `0x00` |
| 5 | Axis 1 (pitch) | `0x7F` (127) |
| 6 | Axis 2 (roll) | `0x7F` (127) |
| 7 | Axis 3 (throttle) | `0x80` (128) |
| 8 | Axis 4 (yaw) | `0x80` (128) |
| 9 | Trim 1 | `0x20` |
| 10 | Trim 2 | `0x20` |
| 11 | XOR checksum of bytes 2-10 | — |

### Flag Byte

| Bit | Value | Command |
|-----|-------|---------|
| 0 | `0x01` | Auto Takeoff |
| 1 | `0x02` | Auto Land |
| 2 | `0x04` | Return to Home |
| 7 | `0x80` | Emergency Stop |

### Axis Values

From native library disassembly (`convertHyControl()` in `libLGDataUtils.so`):
- Range: 0-255
- Center: 127/127/128/128 (pitch/roll/throttle/yaw)
- **Note:** The stock app never sends non-center values over WiFi. Variable axis control is structurally supported but untested.

### Command Burst Pattern (from capture)

```
# Takeoff: 50 packets with flag=0x01 at 20ms intervals
5A 55 08 02 01 7F 7F 80 80 20 20 0B    × 50

# Then 25 idle packets with flag=0x00
5A 55 08 02 00 7F 7F 80 80 20 20 0A    × 25

# GPS packets interleaved every 5th control packet
```

## 6. XOR Checksum

Used by both GPS and flight control packets:

```python
def xor_checksum(packet, start, end):
    result = 0
    for i in range(start, end):
        result ^= packet[i]
    return result
```

- GPS packet: `checksum = xor(bytes[2:18])` → stored in byte 18
- Flight control: `checksum = xor(bytes[2:11])` → stored in byte 11

## 7. Drone Responses (UDP 8080)

| Response | Meaning | When |
|----------|---------|------|
| `"1080P"` | Video resolution | After `0x0F` query |
| `"V3.8.2"` | Firmware version | After `(BG,` handshake |
| `"timeok"` | Clock synced | After time sync packet |
| `"forceI"` | Keyframe sent | After `0x27` request |
| `"drone0\n"` | Device identifier | During handshake |
| `"ok "` | Heartbeat ACK | Every 1s during heartbeat |
| `"ok1"` | Command ACK type 1 | Various |
| `"ok2"` | Command ACK type 2 | Various |

## 8. Video Stream (TCP 8888)

### Connection
1. TCP connect to `172.16.10.1:8888`
2. Send 11-byte heartbeat: `01 02 03 04 05 06 07 08 09 28 28`
3. Repeat heartbeat every 1 second (keepalive)
4. Drone starts streaming immediately

### Stream Format
- **Codec:** H.264, Main profile, YUV420P
- **Resolution:** 1280×720
- **Framerate:** 25 fps
- **Wrapper:** Custom 44-byte frame headers (`00 00 01 A0` + 40 bytes metadata)
- **Playback:** Strip headers, pipe raw H.264 NAL units to any decoder

### Header Stripping
```
Custom header:  00 00 01 A0 [40 bytes metadata]
H.264 NAL unit: 00 00 00 01 [frame data]
```
Remove everything between `00 00 01 A0` markers up to the next `00 00 00 01` start code.

## 9. HiSi Camera Commands (UDP 8088)

Stateless UDP — no handshake or connection setup. Fire and forget with optional retry.

### Packet Format
```
FF 35 19 0A [CMD_ID] [optional payload...]
```

### Response Format
Response echoes the CMD_ID. Byte 5 = `0xFF` means error.

### Command Table (from decompiled `HisiCommandHelper.java`)

| CMD ID | Name | Description |
|--------|------|-------------|
| 2 | getSensor | Query camera sensor info |
| 5 | getWorkMode | Get current mode |
| 10 | getContrast | Get contrast level |
| 12 | getSaturation | Get saturation level |
| 31 | getRecordResolution | Get recording resolution |
| 33 | getZoom | Get zoom level |
| 34 | recordStart | Start video recording |
| 35 | recordStop | Stop video recording |
| 45 | getAntiShake | Get stabilization status |
| 46 | getSDCardInfo | Get SD card info |
| 54 | getTemperature | Get sensor temperature |
| 61 | photoSingle | Take a single photo |
| 74 | getWifiChannel | Get WiFi channel |

### Retry Logic (from stock app)
- Send command, wait 500ms for response
- Retry up to 5 times if no response
- Stock app sends `getSensor` (cmd 2) first, then `getRecordResolution` (cmd 31) at startup

## 10. Single-Byte Commands (UDP 8080)

| Byte | ASCII | Purpose | Response |
|------|-------|---------|----------|
| `0x0F` | — | Resolution query | `"1080P"` |
| `0x27` | `'` | Force video keyframe | `"forceI"` |
| `0x28` | `(` | Version handshake (byte 1/4) | — |
| `0x42` | `B` | Version handshake (byte 2/4) | — |
| `0x47` | `G` | Version handshake (byte 3/4) | — |
| `0x2C` | `,` | Version handshake (byte 4/4) | `"V3.8.2"` |

## 11. Important Notes

### WiFi vs Physical RC
The physical RC communicates via 2.4GHz radio directly to the flight controller. WiFi commands go through the Fullhan SoC. These are **completely independent** — the WiFi subsystem has no visibility into RC state.

The RC must arm the motors before WiFi takeoff commands work. We have not yet found a WiFi motor-arm command.

### Flight Readiness Sequence
1. Connect to drone WiFi
2. Perform handshake (Section 3)
3. Start heartbeat at 1Hz
4. Send GPS/status packets at 5Hz with decreasing accuracy values
5. Wait for accuracy to reach ≤ 3 (or just send 3 from the start — works)
6. Send flight control burst (takeoff/land/etc.)
7. Continue GPS + heartbeat during flight

### Known Limitations
- RC must arm motors before WiFi takeoff works
- Takeoff produces slight diagonal drift (trim calibration issue)
- No telemetry over WiFi — drone only sends `"ok"` ACKs
- Telnet access triggers MAC-specific ban (power cycle to clear)
- RTSP port 554 does not deliver frames despite being open

## 12. Decompiled Source Reference

Key classes from the stock APK (`com.vison.macrochip.sj.hs.gps.v1`):

| Class | Purpose |
|-------|---------|
| `com.vison.baselibrary.hisi.HisiCommandHelper` | HiSi camera command construction |
| `com.vison.baselibrary.connect.wifi.MsgUdpConnection` | UDP transport + heartbeat |
| `com.vison.baselibrary.hisi.SendDataManager` | Command retry queue (5× at 500ms) |
| `com.vison.macrochip.sj.gps.pro.thread.SendControlThread` | Flight control packet builder |
| `com.vison.macrochip.sdk.LGDataUtils` | Native JNI — `convertHyControl()` |

### Port Routing (from `BaseApplication.writeUDPCmd`)
- Packets starting with `FF 35 19` → **port 8088** (HiSi camera)
- Everything else → **port 8080** (flight/GPS/init)

## 13. Related Protocol (TurboDrone / S2x)

The [TurboDrone](https://github.com/marshallrichards/turbodrone) project documents the "S2x" variant of this protocol family (S20/S29/K417 drones). Same app, same ports, but different wire format:

| Feature | S2x (TurboDrone) | H120D (Heliway "Hy") |
|---------|-------------------|-----------------------|
| Control packet | 20 bytes (`0x66...0x99`) | 12 bytes (`5A 55 08 02...`) |
| Axis bytes | Positions 2-5 | Positions 5-8 |
| Video keepalive | `0x08 + IP` every 2s | 11-byte heartbeat at 1Hz |
| Speed field | Byte 1 | Not present |
| Checksum range | bytes[2:18] | bytes[2:N-1] |

The H120D uses `LGDataUtils.convertHyControl()` (Heliway) rather than `convertControl()` (S2x).
