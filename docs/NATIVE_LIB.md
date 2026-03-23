# Native Library Reversing — `libLGDataUtils.so`

ARM disassembly of the joystick encoding function from the stock APK's native library.

**File:** `lib/armeabi-v7a/libLGDataUtils.so` (132 KB)
**Function:** `convertHyControl()` at offset `0xa6e9`, 1648 bytes ARM Thumb

## What It Does

`convertHyControl()` takes an `LGControlHyBean` Java object (joystick positions + button flags) and produces a 13-byte `byte[]` used as the payload for flight control packets.

The "Hy" stands for **Heliway** — the flight controller manufacturer. Other Holy Stone drones use `convertControl()` (the "S2x" variant) which produces a different format.

## Output Format (13 bytes)

| Byte | Field | Center Value | Range | Description |
|------|-------|-------------|-------|-------------|
| 0 | rocker1 | 0x7F (127) | 0-255 | Pitch axis |
| 1 | rocker2 | 0x7F (127) | 0-255 | Roll axis |
| 2 | rocker3 | 0x80 (128) | 0-255 | Throttle axis |
| 3 | rocker4 | 0x80 (128) | 0-255 | Yaw axis |
| 4-7 | flags | 0x00 | bitfield | Control flags (see below) |
| 8-11 | ext_flags | 0x00 | bitfield | Extended flags |
| 12 | overflow | 0x00 | — | High bits overflow |

## Flag Bitfield (bytes 4-7)

```
Byte 4:
  bits[0:5]  — trim1 (6 bits, 0-63)
  bits[6:11] — trim2 (6 bits, 0-63)
  bit 12     — speed
  bit 13     — noHead (headless mode)
  bit 14     — roll (flip mode)
  bit 15     — gpsMode

Byte 5:
  bit 16     — levelCor (level correction)
  bit 17     — magCor (magnetometer correction)
  bit 18     — lockUnlock (motor arm/disarm?)
  bit 19     — autoTakeoff
  bit 20     — autoLand
  bit 21     — goHome
  bit 22     — stop (emergency stop)
  bit 23     — followMe

Byte 6:
  bit 24     — circleFly
  bit 25     — popublicFly (waypoint?)
  bit 26     — photo
  bit 27     — video
  bits[28:29] — PTZ_H (pan, 2 bits)
  bits[30:31] — PTZ_V (tilt, 2 bits)

Byte 7:
  bit 32     — ctrlPanel
  bit 33     — display
  bit 34     — OpticalFlowOn
  bit 35     — VisionFollow
  bit 36     — Dis_valid
  bits[37:63] — reserved
```

## Critical Finding

**`convertHyControl()` is declared but never called from Java code.**

The stock app's `SendControlThread` constructs 12-byte flight control packets directly in Java, hardcoding:
- Axes to center values (0x7F, 0x7F, 0x80, 0x80)
- Only flags: takeoff (0x01), land (0x02), goHome (0x04), stop (0x80)
- Trims to 0x20

This means the stock app **does not send variable joystick values over WiFi**. The RC handles all actual flight control via 2.4GHz radio. The WiFi path only triggers high-level commands.

The native function exists (and is fully functional) because other drone models in the same app family do use it. The H120D's firmware likely accepts variable axis values — it just never receives them from the stock app.

## Wire Format Comparison

The 13-byte native output maps to the 12-byte wire packet as:

```
Native bytes[0:3]  → Wire bytes[5:8]   (axes)
Native bytes[4:7]  → Wire byte[4]      (flags, compressed to 1 byte)
                      Wire bytes[9:10]  (trims, extracted from bitfield)
                      Wire byte[11]     (XOR checksum, computed)
```

The stock app simplifies this by skipping the native library entirely and building the 12-byte packet directly.
