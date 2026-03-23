#!/usr/bin/env python3
"""
H120D HiSi Camera Commands (UDP 8088)
=======================================
Sends camera/sensor commands to the drone's HiSilicon subsystem.
Stateless UDP — no handshake or connection setup needed.

Usage:
    python hisi_camera.py                # Run all read-only queries
    python hisi_camera.py photo          # Take a photo
    python hisi_camera.py record start   # Start video recording
    python hisi_camera.py record stop    # Stop video recording
    python hisi_camera.py cmd <id>       # Send raw command by ID

Prerequisites:
    - Connect to drone WiFi first
    - Props can be off for queries, should be on for photo/video
"""
import socket
import sys
import time

DRONE_IP = "172.16.10.1"
HISI_PORT = 8088
HEADER = bytes([0xFF, 0x35, 0x19, 0x0A])

# Command IDs (from decompiled HisiCommandHelper.java)
COMMANDS = {
    "getSensor":          2,
    "getWorkMode":        5,
    "getContrast":        10,
    "getSaturation":      12,
    "getRecordResolution": 31,
    "getZoom":            33,
    "recordStart":        34,
    "recordStop":         35,
    "getAntiShake":       45,
    "getSDCardInfo":      46,
    "getTemperature":     54,
    "photoSingle":        61,
    "getWifiChannel":     74,
}


def build_cmd(cmd_id, payload=b""):
    """Build a HiSi command packet."""
    return HEADER + bytes([cmd_id]) + payload


def send_cmd(sock, name, cmd_id, payload=b"", timeout=2.0):
    """Send a command and print the response."""
    pkt = build_cmd(cmd_id, payload)
    print(f"\n--- {name} (cmd {cmd_id}) ---")
    print(f"  TX: {pkt.hex()}")
    sock.sendto(pkt, (DRONE_IP, HISI_PORT))

    sock.settimeout(timeout)
    try:
        data, addr = sock.recvfrom(4096)
        print(f"  RX: {data.hex()}")
        if len(data) > 5 and data[5] == 0xFF:
            print(f"  STATUS: ERROR (byte[5] = 0xFF)")
        else:
            print(f"  STATUS: OK")
        return data
    except socket.timeout:
        print(f"  STATUS: NO RESPONSE (timeout {timeout}s)")
        return None


def run_queries(sock):
    """Run all safe read-only queries."""
    read_only = [
        ("getSensor", 2, b""),
        ("getRecordResolution", 31, b""),
        ("getWorkMode", 5, b""),
        ("getSDCardInfo", 46, b""),
        ("getTemperature", 54, b"\x00"),
        ("getAntiShake", 45, b""),
        ("getWifiChannel", 74, b""),
        ("getContrast", 10, b""),
        ("getSaturation", 12, b""),
        ("getZoom", 33, b""),
    ]
    for name, cmd_id, payload in read_only:
        send_cmd(sock, name, cmd_id, payload)
        time.sleep(0.5)


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", 0))

    print("=== H120D HiSi Camera Commands (UDP 8088) ===")
    print(f"Target: {DRONE_IP}:{HISI_PORT}\n")

    if len(sys.argv) < 2:
        run_queries(sock)

    elif sys.argv[1] == "photo":
        send_cmd(sock, "photoSingle", 61, b"\x00")

    elif sys.argv[1] == "record":
        if len(sys.argv) > 2 and sys.argv[2] == "stop":
            send_cmd(sock, "recordStop", 35)
        else:
            send_cmd(sock, "recordStart", 34)

    elif sys.argv[1] == "cmd" and len(sys.argv) > 2:
        cmd_id = int(sys.argv[2])
        payload = bytes.fromhex(sys.argv[3]) if len(sys.argv) > 3 else b""
        send_cmd(sock, f"raw_cmd_{cmd_id}", cmd_id, payload)

    else:
        print(f"Unknown: {sys.argv[1]}")
        print("Usage: python hisi_camera.py [photo | record start/stop | cmd <id> [payload_hex]]")

    sock.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
