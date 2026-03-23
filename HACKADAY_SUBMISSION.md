# Hackaday Submission: Reverse-Engineering the Holy Stone H120D Drone Protocol

> **Title:** Reverse-Engineering a Consumer GPS Drone: From APK Teardown to Autonomous Flight
>
> **Subtitle:** The Holy Stone H120D runs RT-Thread on a Fullhan SoC, bans your MAC if you telnet in, and doesn't actually use its own joystick code
>
> **GitHub:** https://github.com/zturner1/h120d-protocol
>
> **Author:** Zac Turner

---

## The Story

I picked up a Holy Stone H120D GPS drone — a mid-range quadcopter with GPS return-to-home, follow-me, and a camera that claims 1080p but actually streams 720p — and wanted to control it from something other than the stock Android app. What started as "just sniff some UDP packets" turned into a deep dive through decompiled Java, ARM disassembly, an RT-Thread RTOS shell, and a telnet server that fights back.

The result: complete protocol documentation, working Python control scripts, an Arduino Nano 33 IoT autonomous controller, and live video streaming — all from scratch, all open source, and the first public documentation of this drone's protocol.

## The Setup

The H120D creates its own WiFi access point (`HolyStoneFPV-XXXXXX`, open network) and hands out DHCP addresses in the `172.16.0.0/16` range. The stock app (`com.vison.macrochip.sj.hs.gps.v1`) talks to the drone at `172.16.10.1` over a handful of ports. My test rig:

- A rooted Samsung Galaxy Tab A9 running the stock app with `tcpdump` capturing traffic
- A PC running jadx to decompile the APK
- An Arduino Nano 33 IoT as an autonomous WiFi bridge
- An ALFA AWUS036NHA for a second WiFi path (so I could stay on my home network while talking to the drone)

## Phase 1: Packet Capture — "It's Just UDP, Right?"

I fired up tcpdump on the tablet while using the stock app normally. The drone uses three main channels:

| Port | Protocol | Purpose |
|------|----------|---------|
| UDP 8080 | Custom binary | Command, GPS, flight control |
| TCP 8888 | H.264 + custom headers | 720p video at 25fps (despite claiming "1080P") |
| UDP 8088 | HiSi binary | Camera/sensor commands |

The handshake on UDP 8080 is charmingly simple. Send a single byte `0x0F`, get back `"1080P"`. Send four single-byte UDP packets — `0x28`, `0x42`, `0x47`, `0x2C` (the ASCII characters `(`, `B`, `G`, `,`) — and get back `"V3.8.2"`. Send a 29-byte timestamp, get `"timeok"`. Then keep a 5-byte heartbeat going at 1Hz or the drone ghosts you.

The flight control packet is 12 bytes:

```
5A 55 08 02 [flags] [pitch] [roll] [throttle] [yaw] [trim1] [trim2] [XOR]
```

Flags: `0x01` = takeoff, `0x02` = land, `0x04` = go home, `0x80` = emergency stop. The app sends these in 50-packet bursts at 50Hz. Simple enough.

But here's the first surprise.

## The GPS Accuracy Gate

Before the stock app sends any flight command, it streams 19-byte GPS packets to the drone for about 26 seconds. I initially saw a "state byte" cycling through values 7→6→5→4→3 and assumed it was a state machine — maybe an arming sequence?

Nope. Those values are **literal GPS accuracy in meters**. The packet contains the *phone's* GPS position (for return-to-home and follow-me), and the drone won't accept takeoff until accuracy hits ≤ 3 meters. The "state transitions" were just my tablet's GPS warming up in my backyard.

Once I figured this out, I set accuracy to 3 from the start. The drone accepted commands immediately.

## Phase 2: The APK Teardown — "Wait, It Never Calls Its Own Function?"

Decompiling the APK with jadx revealed the full class hierarchy. The interesting stuff lives in `com.vison.baselibrary` and a native library called `libLGDataUtils.so`.

I spent a day disassembling `convertHyControl()` — a 1,648-byte ARM Thumb function in the native library that converts joystick positions into a 13-byte control payload. It maps four analog axes (0-255, centered at 127/128), encodes 20+ bitfield flags (speed, headless mode, follow-me, circle fly, optical flow, PTZ control...), and produces a nicely packed output.

Then I searched the Java code for where it's called.

**It isn't.**

The stock app's `SendControlThread` builds the 12-byte flight packet entirely in Java, hardcoding all four axes to their center values. The app literally never sends variable joystick data over WiFi. It only sends high-level flags: takeoff, land, go home, stop.

All actual flight control comes from the physical RC transmitter via 2.4GHz radio, completely bypassing WiFi. The function exists in the native library because other drones in the same app family (the "S2x" models) use it — the H120D just... doesn't.

## Phase 3: Telnet — "Did It Just Ban Me?"

Port scan revealed TCP port 23 open. Connecting drops you into an RT-Thread 2.1.0 finsh shell:

```
 \ | /
- RT -     Thread Operating System
 / | \     2.1.0 build Oct  1 2019
 2006 - 2017 Copyright by rt-thread team
finsh />
```

RT-Thread! Not Linux, not a bare-metal loop — a proper RTOS with threads, semaphores, mailboxes, the works. `list_thread()` revealed 28 threads: motor control, sensor fusion, GPS, WiFi command handling, video pipeline, and more. `list_device()` showed 30 peripherals.

And every device name started with `fh_`: `fh_flash`, `fh_clock`, `fh_wdt0`, `fh_dma0`, `fh_isp`, `fh_jpeg`, `fh_vpu`...

**The chip isn't HiSilicon.** Despite the app's Java package names (`hisi.*`), the actual SoC is a **Fullhan Micro** — likely an FH8620 or FH8830, a Chinese IP camera chip. ARM9/ARM11 core, memory mapped at `0xa0xxxxxx`. The `HisiCommandHelper` class in the app is a misnomer carried over from the protocol family.

But here's the nasty part. After my first telnet session, the drone stopped responding to my ALFA adapter. Completely. Other devices on the same WiFi worked fine. Power cycling the drone fixed it.

**The drone bans your MAC address when you connect to telnet.** Not temporarily — it blackholes all traffic from that MAC until a full power cycle. I confirmed this by connecting with the ALFA (banned), then immediately connecting with the Arduino Nano 33 IoT (different MAC, worked perfectly). The ban is MAC-specific.

### The Workaround: Arduino Telnet Bridge

Since the Arduino has a different MAC and never gets banned, I built a telnet bridge. Serial commands `TOPEN`, `TCMD <command>`, and `TCLOSE` tunnel finsh commands through the Arduino's connection. Unlimited sessions, no power cycle needed. The only downside: the Arduino's limited buffer truncates responses longer than ~800 bytes.

For big dumps, I do a "blitz" — connect directly with the ALFA, fire commands as fast as possible, accept the ban, power cycle. One blitz captured the full thread list, device list, filesystem contents, semaphores, mutexes, timers, and event flags in 5 seconds.

## Phase 4: Video — "That's Not RTSP"

Port 554 (RTSP) accepts connections but never delivers any frames. The real video path is TCP 8888. Send an 11-byte heartbeat (`01 02 03 04 05 06 07 08 09 28 28`), keep it going at 1Hz, and the drone pushes H.264 wrapped in custom 44-byte frame headers.

The headers start with `00 00 01 A0` followed by 40 bytes of metadata. Standard H.264 NAL units start with `00 00 00 01`. Strip one, keep the other, pipe to ffplay:

```bash
python live_video.py
```

1280×720, 25fps, Main profile, YUV420P. Clean enough to pipe straight to ffmpeg for recording.

## Phase 5: First Flight

With the protocol fully mapped, I built an autonomous controller on the Arduino Nano 33 IoT. It connects to the drone WiFi, performs the full handshake, streams GPS packets at 5Hz, maintains heartbeat at 1Hz, and sends flight command bursts on serial command.

Type `AUTO` and it runs the full sequence: handshake → GPS stream (5 seconds at accuracy=3) → takeoff burst (75 packets at 50Hz). **It worked.** The drone lifted off from a WiFi command sent by an Arduino.

One catch: the physical RC still needs to arm the motors first. I haven't found a WiFi motor-arm command yet — the `lockUnlock` flag exists in the native library's bitfield but the app never sets it. That's on the to-do list.

The takeoff also produces a slight diagonal drift, likely from the hardcoded trim values (`0x20, 0x20`). The stock app presumably calibrates trims at startup, but I haven't captured that sequence yet.

## What's Published

Everything is on GitHub: **[zturner1/h120d-protocol](https://github.com/zturner1/h120d-protocol)**

- Full protocol reference (packet formats, handshake, every command)
- Python controller (stdlib only — zero pip installs)
- Live video viewer (TCP 8888 → ffplay)
- HiSi camera command tool (photo, video, sensor queries)
- Arduino Nano 33 IoT controller with telnet bridge
- RT-Thread RTOS internals (threads, devices, filesystem)
- Native library disassembly notes

This is the first public documentation of the H120D's protocol. The drone shares a protocol family with other Holy Stone/Vison models (S20, S29, K417), but uses the "Heliway Hy" wire format variant rather than the S2x format documented by TurboDrone.

## Open Questions

- **Motor arm over WiFi** — The `lockUnlock` flag exists but is untested. Finding this would eliminate the RC dependency entirely.
- **TCP 8830** — Discovered via `list_tcps()` in the finsh shell. Something is listening. Haven't connected yet.
- **Full `list()` output** — The finsh function table is longer than 800 bytes. Need a clean blitz capture to get all callable functions.
- **Variable axis control** — The native library supports it, the wire format supports it, the firmware probably supports it. Nobody's ever sent non-center axis values over WiFi to this drone.
- **Diagonal takeoff** — Trim calibration or missing gyro cal command? Need to capture what the stock app sends before first flight after power-on.

If you have an H120D (or any `com.vison.macrochip` drone), grab the scripts and try it. PRs welcome.

---

*Submit to: https://hackaday.com/submit-a-tip/ or post as a project on hackaday.io*
*Use the shorter version below for the tip submission form, link to the full writeup.*

---

## Short Version (for tip submission form)

**Title:** Reverse-Engineering the Holy Stone H120D GPS Drone — Complete Protocol Docs and Autonomous Flight

I reverse-engineered the WiFi protocol of the Holy Stone H120D GPS drone by decompiling the stock Android app, capturing packets, disassembling the native ARM library, and exploring the onboard RT-Thread RTOS via telnet.

Key findings: The drone runs a Fullhan Micro SoC (not HiSilicon as the app's code suggests), bans your MAC address if you connect to its telnet shell, and the stock app never actually sends joystick data over WiFi — it only sends takeoff/land flags with axes hardcoded to center.

Published complete protocol docs, working Python/Arduino control scripts, and achieved autonomous takeoff/landing over WiFi. First public documentation of this drone's protocol.

GitHub: https://github.com/zturner1/h120d-protocol
