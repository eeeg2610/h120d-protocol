#!/usr/bin/env python3
"""
H120D Live Video Viewer
========================
Connects to TCP 8888, strips custom frame headers, pipes clean H.264 to ffplay.

Usage:
    python live_video.py

Prerequisites:
    - ffplay (from ffmpeg) must be in your PATH
    - Connect to drone WiFi first
    - No handshake needed on TCP 8888 — just heartbeat

Press 'q' in the video window or Ctrl+C to stop.
"""
import socket
import subprocess
import sys
import threading
import time

DRONE_IP = "172.16.10.1"
TCP_PORT = 8888

# 11-byte heartbeat — send on connect, then every 1 second
HEARTBEAT = bytes([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x28, 0x28])

# Custom frame header (44 bytes): 00 00 01 A0 + 40 bytes metadata
CUSTOM_HEADER = b'\x00\x00\x01\xa0'

# Standard H.264 NAL unit start code
H264_START = b'\x00\x00\x00\x01'


def strip_custom_headers(data):
    """Remove 44-byte custom frame headers, keep only H.264 NAL units."""
    clean = bytearray()
    i = 0
    while i < len(data) - 4:
        if data[i:i+4] == CUSTOM_HEADER:
            # Skip metadata until next H.264 start code
            j = i + 4
            while j < len(data) - 4:
                if data[j:j+4] == H264_START:
                    break
                j += 1
            if j < len(data) - 4:
                i = j
            else:
                break
        else:
            clean.append(data[i])
            i += 1
    clean.extend(data[i:])
    return bytes(clean)


def main():
    # Connect to drone video port
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect((DRONE_IP, TCP_PORT))
    except Exception as e:
        print(f"Cannot connect to drone: {e}")
        print("Make sure you're connected to the drone's WiFi.")
        sys.exit(1)

    print(f"Connected to {DRONE_IP}:{TCP_PORT}")
    sock.sendall(HEARTBEAT)

    # Heartbeat keepalive thread
    def keepalive():
        while True:
            try:
                sock.sendall(HEARTBEAT)
                time.sleep(1)
            except Exception:
                break
    threading.Thread(target=keepalive, daemon=True).start()

    # Launch ffplay
    print("Launching video window (1280x720 H.264 25fps)...")
    ffplay = subprocess.Popen(
        [
            "ffplay",
            "-f", "h264",
            "-probesize", "65536",
            "-analyzeduration", "500000",
            "-flags", "low_delay",
            "-framedrop",
            "-window_title", "H120D Live Video",
            "-loglevel", "warning",
            "-i", "-",
        ],
        stdin=subprocess.PIPE,
    )

    leftover = b''
    bytes_total = 0

    try:
        while ffplay.poll() is None:
            data = sock.recv(65536)
            if not data:
                break

            data = leftover + data
            leftover = b''

            # Save partial header/start code at end for next iteration
            for tail_len in range(1, 5):
                if len(data) >= tail_len and data[-tail_len:] in [
                    CUSTOM_HEADER[:tail_len], H264_START[:tail_len]
                ]:
                    leftover = data[-tail_len:]
                    data = data[:-tail_len]
                    break

            clean = strip_custom_headers(data)
            if clean:
                try:
                    ffplay.stdin.write(clean)
                    ffplay.stdin.flush()
                    bytes_total += len(clean)
                except BrokenPipeError:
                    break

    except KeyboardInterrupt:
        print("\nStopping...")
    except ConnectionResetError:
        print("Drone disconnected.")
    finally:
        sock.close()
        if ffplay.poll() is None:
            ffplay.terminate()
        print(f"Stream ended. Total: {bytes_total / 1024:.0f} KB")


if __name__ == "__main__":
    main()
