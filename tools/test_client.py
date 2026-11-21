#!/usr/bin/env python3
"""
ESP32 Headless Smart Coffee Scale - Test & Validation Client
Compatible with Decent Scale BLE Protocol (Beanconqueror)

Features:
- Scans and automatically discovers "Decent Scale"
- Subscribes to 10 Hz weight stream on characteristic 0xFFF4
- Validates 7-byte packet structure and XOR checksum integrity
- Unpacks signed big-endian 16-bit weight (decigrams -> grams)
- Calculates real-time flow rate (g/s) and packet rate (Hz)
- Allows remote Tare execution via characteristic 0xFFF3
"""

import sys
import time
import struct
import asyncio
import argparse
from typing import Optional

try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.characteristic import BleakGATTCharacteristic
except ImportError:
    print("[ERROR] 'bleak' library is not installed.")
    print("Please install bleak via: pip install bleak")
    sys.exit(1)

# =============================================================================
# Decent Scale BLE UUIDs & Protocol Constants
# =============================================================================
TARGET_DEVICE_NAME    = "Decent Scale"
UUID_DECENT_SERVICE   = "0000fff0-0000-1000-8000-00805f9b34fb"
UUID_DECENT_NOTIFY    = "0000fff4-0000-1000-8000-00805f9b34fb"
UUID_DECENT_WRITE     = "0000fff3-0000-1000-8000-00805f9b34fb"

# Decent Scale Command Codes
DECENT_HEADER_BYTE    = 0x03
DECENT_TYPE_STABLE    = 0xCE
DECENT_TYPE_MOVING    = 0xCA
DECENT_CMD_TARE       = 0x0F

# Remote Tare Command Packet:
# [0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, Checksum: 0x03 ^ 0x0F = 0x0C]
TARE_PACKET = bytearray([0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0C])

# =============================================================================
# State Tracking
# =============================================================================
class ScaleMetrics:
    def __init__(self):
        self.packet_count = 0
        self.checksum_errors = 0
        self.last_packet_time = 0.0
        self.last_weight = 0.0
        self.flow_rate = 0.0
        self.hz_rate = 0.0
        self.hz_counter = 0
        self.hz_timer = time.time()

metrics = ScaleMetrics()


def compute_xor_checksum(data: bytearray) -> int:
    """Computes XOR checksum across the first 6 bytes of a 7-byte packet."""
    chk = 0
    for b in data[:6]:
        chk ^= b
    return chk


def notification_handler(characteristic: BleakGATTCharacteristic, data: bytearray, raw_mode: bool = False):
    """Parses incoming 7-byte notifications from the Decent Scale."""
    now = time.time()
    metrics.packet_count += 1
    metrics.hz_counter += 1

    # Calculate packet transmission frequency (Hz)
    if now - metrics.hz_timer >= 1.0:
        metrics.hz_rate = metrics.hz_counter / (now - metrics.hz_timer)
        metrics.hz_counter = 0
        metrics.hz_timer = now

    packet_len = len(data)
    if packet_len != 7:
        print(f"\n[INVALID PACKET] Unexpected length {packet_len} bytes: {data.hex()}")
        return

    # Checksum validation
    expected_chk = compute_xor_checksum(data)
    actual_chk = data[6]
    is_checksum_valid = (expected_chk == actual_chk)

    if not is_checksum_valid:
        metrics.checksum_errors += 1
        print(f"\n[CHECKSUM FAIL] Packet: {data.hex()} | Expected: 0x{expected_chk:02X}, Got: 0x{actual_chk:02X}")
        return

    # Header check
    header = data[0]
    status_type = data[1]
    
    # Weight decoding: signed 16-bit big-endian int at bytes 2-3
    raw_decigrams = struct.unpack(">h", data[2:4])[0]
    weight_grams = raw_decigrams / 10.0

    # Flow rate calculation (g/s)
    dt = now - metrics.last_packet_time if metrics.last_packet_time > 0 else 0.1
    if dt > 0.05:
        metrics.flow_rate = (weight_grams - metrics.last_weight) / dt
        metrics.last_weight = weight_grams
        metrics.last_packet_time = now

    # Stability indicator
    status_str = "STABLE" if status_type == DECENT_TYPE_STABLE else "MOVING"

    if raw_mode:
        print(f"[{now:.3f}] RAW: {data.hex().upper()} | Weight: {weight_grams:6.2f}g | Status: {status_str} | Chk: OK")
    else:
        # Formatted continuous terminal dashboard
        sys.stdout.write(
            f"\r[SCALE LIVE] Weight: {weight_grams:7.2f} g | "
            f"Flow: {metrics.flow_rate:5.2f} g/s | "
            f"Status: {status_str:<6} | "
            f"Freq: {metrics.hz_rate:4.1f} Hz | "
            f"Pkts: {metrics.packet_count} (Errors: {metrics.checksum_errors})"
        )
        sys.stdout.flush()


async def scan_for_scale(timeout: float = 6.0) -> Optional[str]:
    """Scans for the Decent Scale BLE peripheral and returns its device address."""
    print(f"[*] Scanning for BLE devices broadcasting '{TARGET_DEVICE_NAME}' or service {UUID_DECENT_SERVICE}...")
    
    devices = await BleakScanner.discover(timeout=timeout)
    for d in devices:
        name = d.name or ""
        service_uuids = [str(u).lower() for u in (d.metadata.get("uuids") or [])]
        
        if TARGET_DEVICE_NAME.lower() in name.lower() or UUID_DECENT_SERVICE.lower() in service_uuids:
            print(f"[+] Found Target Scale: '{name}' [{d.address}]")
            return d.address

    print("[-] Device not found in scan results. Discovered devices:")
    for d in devices:
        if d.name:
            print(f"    - {d.name} [{d.address}]")
    return None


async def run_client(address: Optional[str], raw_mode: bool, do_tare: bool, duration: Optional[float]):
    if not address:
        address = await scan_for_scale()
        if not address:
            print("[ERROR] Could not find 'Decent Scale'. Ensure ESP32 is powered on and within range.")
            return

    print(f"[*] Connecting to {address}...")
    async with BleakClient(address) as client:
        if not client.is_connected:
            print("[ERROR] Connection failed.")
            return

        print(f"[+] Connected successfully to {address}!")

        # Verify services & characteristics
        services = client.services
        notify_char = services.get_characteristic(UUID_DECENT_NOTIFY)
        write_char = services.get_characteristic(UUID_DECENT_WRITE)

        if not notify_char:
            print(f"[ERROR] Notification characteristic {UUID_DECENT_NOTIFY} not found!")
            return

        if not write_char:
            print(f"[WARNING] Write characteristic {UUID_DECENT_WRITE} not found!")

        # Optional startup tare or wakeup command
        if do_tare and write_char:
            print("[*] Sending startup Tare command...")
            await client.write_gatt_char(write_char, TARE_PACKET, response=False)
            await asyncio.sleep(0.2)

        # Enable notifications
        print(f"[*] Subscribing to weight stream notifications on {UUID_DECENT_NOTIFY}...")
        await client.start_notify(
            notify_char,
            lambda c, d: notification_handler(c, d, raw_mode=raw_mode)
        )

        print("[+] Weight streaming active! Press Ctrl+C to terminate.")
        print("-" * 85)

        start_time = time.time()
        try:
            while True:
                await asyncio.sleep(0.5)
                if duration and (time.time() - start_time >= duration):
                    print(f"\n[*] Specified duration of {duration}s reached. Exiting cleanly.")
                    break
        except (KeyboardInterrupt, asyncio.CancelledError):
            print("\n[*] Stopping notifications...")
        finally:
            await client.stop_notify(notify_char)
            print("[+] Disconnected.")


def main():
    parser = argparse.ArgumentParser(
        description="BLE Test Client for ESP32 Headless Smart Coffee Scale (Decent Protocol)"
    )
    parser.add_argument("-a", "--address", type=str, default=None,
                        help="Target device Bluetooth MAC / UUID address (auto-scans if omitted)")
    parser.add_argument("-r", "--raw", action="store_true",
                        help="Display raw hex packets instead of the dashboard line")
    parser.add_argument("-t", "--tare", action="store_true",
                        help="Send Tare command immediately upon connection")
    parser.add_argument("-d", "--duration", type=float, default=None,
                        help="Run for N seconds and then disconnect")

    args = parser.parse_args()

    try:
        asyncio.run(run_client(
            address=args.address,
            raw_mode=args.raw,
            do_tare=args.tare,
            duration=args.duration
        ))
    except KeyboardInterrupt:
        print("\n[*] Interrupted by user.")


if __name__ == "__main__":
    main()
