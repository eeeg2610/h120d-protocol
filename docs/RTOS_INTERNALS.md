# RT-Thread RTOS Internals

Findings from the drone's telnet shell (TCP port 23, finsh C-expression interpreter).

**SoC: Fullhan Micro** (FH8620/FH8830 family) — identified by `fh_` device prefixes.
Originally assumed HiSilicon based on the app's class names, but the actual chip is Fullhan.

---

## Connection

```
 \ | /
- RT -     Thread Operating System
 / | \     2.1.0 build Oct  1 2019
 2006 - 2017 Copyright by rt-thread team
finsh />
```

- **OS:** RT-Thread 2.1.0
- **Build:** October 1, 2019
- **Shell:** finsh (C-expression mode only)
- **Syntax:** Use function-call style with parentheses — `list_thread()`, `ls("/picture")`
- **Warning:** Connecting to port 23 triggers a **MAC-level ban** on the connecting device. The drone will refuse all traffic from that MAC until power-cycled.

### Shell Quirks
- `list_thread()` works — bare name, not C-expression
- `list()` shows the function table (use this instead of `list_fn()`)
- `list_fn()` / `list_var()` → "Null node" (no symbols registered)
- `help` → "Unknown symbol" (not a registered command)
- `rt_thread_self()` → "Null node" (runtime C-expression calls don't work)
- `hello()` → "Hello RT-Thread!" (confirms registered functions do work)
- `md` (memory dump) → "Invalid token" (not available)
- `free`, `ps`, `ifconfig` → "Unknown symbol" (MSH commands not registered)

## Threads (28 total)

Captured via `list_thread()`:

| Thread | Priority | Status | Stack | Purpose |
|--------|----------|--------|-------|---------|
| tshell | 20 | ready | 38% | Telnet/finsh shell |
| tidle | 31 | ready | 16% | RTOS idle task |
| motor_thr | 8 | suspend | 19% | Motor control |
| sensor_thr | 9 | suspend | 27% | Sensor fusion |
| gps_thread | 10 | suspend | 17% | GPS processing |
| wifi_cmd_t | 12 | suspend | 23% | WiFi command handler |
| wifi_sta_t | 12 | suspend | 18% | WiFi station management |
| video_thr | 11 | suspend | 31% | Video pipeline |
| cmd_parse | 13 | suspend | 21% | Command parser |
| telnet_thr | 15 | suspend | 24% | Telnet server |
| *+ ~18 more* | — | — | — | Various system threads |

Priority 8 (motor) is highest user priority — motor control takes precedence over everything else.

## Devices (30 total)

Captured via `list_device()`:

| Device | Type | Purpose |
|--------|------|---------|
| fh_flash | Block | SPI flash (firmware) |
| fh_clock | Misc | System clock |
| fh_wdt0 | Misc | Watchdog timer |
| fh_dma0 | Misc | DMA controller |
| fh_gpio | Misc | GPIO pins |
| fh_spi0, fh_spi1 | SPI | SPI buses |
| fh_i2c0, fh_i2c1 | I2C | I2C buses (sensors) |
| fh_uart0, fh_uart1 | Char | UARTs (debug, GPS?) |
| fh_pwm | Misc | PWM (motor control) |
| fh_isp | Misc | Image signal processor |
| fh_jpeg | Misc | JPEG encoder |
| fh_vpu | Misc | Video processing unit |
| fh_sd | Block | SD card |
| *+ ~16 more* | — | Various peripherals |

The `fh_` prefix confirms **Fullhan Micro** SoC, not HiSilicon. Memory addresses in the 0xa0xxxxxx range suggest ARM9/ARM11 core.

## Network Configuration

From `list_if()`:
```
AP mode
MAC:     38:01:46:62:bb:71
IP:      172.16.10.1
Netmask: 255.255.0.0
```

From `list_tcps()`:
| Port | State | Notes |
|------|-------|-------|
| TCP 23 | LISTEN | Telnet (finsh shell) |
| TCP 8888 | LISTEN | Video stream |
| TCP 8830 | LISTEN | **Unknown — unexplored** |

## Filesystem

From `ls("/")`:
```
/picture    — Photo storage
/video      — Video storage
```

From `ls("/picture")`:
```
IMG_20260320_*.jpg    — JPEG photos from camera
```

From `ls("/video")`:
```
VID_20260318_*.avi    — AVI video files (~52MB each)
```

## Known Functions (partial `list()` output)

Captured via Arduino telnet bridge (truncates at ~800 bytes):

| Function | Purpose |
|----------|---------|
| `dhcpd_start` | Start DHCP server |
| `dhcpc_start` | Start DHCP client |
| `dhcpc_stop` | Stop DHCP client |
| `list_tcps` | List TCP connections |
| `list_if` | List network interfaces |
| `set_dns` | Configure DNS |
| `set_if` | Configure network interface |
| `set_mac` | Set MAC address |
| `set_ip6_if` | Configure IPv6 |
| `hello` | Test function ("Hello RT-Thread!") |
| `list_thread` | List threads |
| `list_device` | List devices |
| `list_sem` | List semaphores |
| `list_mutex` | List mutexes |
| `list_timer` | List timers |
| `list_event` | List events |
| `list_mailbox` | List mailboxes |
| `ls` | List directory |

The full function table is longer — the Arduino bridge truncates at ~800 bytes. A direct WiFi connection can capture the complete list but triggers the MAC ban.

## Telnet Ban Workaround

The drone bans the MAC address of any device that connects to port 23. Two workarounds:

1. **Arduino bridge** — Use an Arduino Nano 33 IoT (different MAC) connected to the drone WiFi. Send telnet commands through the Arduino's serial interface (`TOPEN` / `TCMD <command>` / `TCLOSE`). Unlimited sessions, no power cycle needed. Downside: truncates responses >800 bytes.

2. **Disposable connection** — Connect directly, run commands fast (blitz), accept the ban. Power cycle drone to clear. Good for capturing large outputs like the full `list()` table.
