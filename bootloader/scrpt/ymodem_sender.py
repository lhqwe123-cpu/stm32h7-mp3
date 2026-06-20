#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YModem Firmware Sender for UART OTA

Based on YModem-1K protocol (Chuck Forsberg).
Reference: XMODEM/YMODEM Protocol Reference, lrzsz, RT-Thread ymodem.

Transfer flow:
  1. Receiver sends 'C' (handshake, CRC mode request)
  2. Sender sends filename packet (SOH 00 + filename+filesize)
  3. Receiver sends ACK + 'C'
  4. Sender sends data packets (STX 01 + 1024 bytes)
  5. Receiver sends ACK
  6. Repeat 4-5 until all data sent
  7. Sender sends EOT
  8. Receiver sends NAK
  9. Sender sends EOT again
  10. Receiver sends ACK + 'C'
  11. Sender sends empty filename packet (end transfer)
  12. Receiver sends ACK

Features:
  - 128/256/1024 byte packet sizes
  - CRC-16/CCITT checksum
  - Handshake negotiation
  - Timeout retransmission
  - Resume support (--resume)
"""

import serial
import sys
import os
import time
import argparse
import signal

# ============================================================
# YModem Protocol Constants
# ============================================================
SOH = 0x01  # 128-byte packet header
STX = 0x02  # 1024-byte packet header
EOT = 0x04  # End of transmission
ACK = 0x06  # Acknowledge
NAK = 0x15  # Negative acknowledge
CAN = 0x18  # Cancel
C   = 0x43  # 'C' - CRC mode request

PACKET_SIZE_128  = 128
PACKET_SIZE_256  = 256
PACKET_SIZE_1024 = 1024

# Timeouts (seconds)
INIT_TIMEOUT    = 60.0   # Initial handshake timeout
PACKET_TIMEOUT  = 3.0    # Data packet timeout
EOT_TIMEOUT     = 10.0   # EOT timeout
ACK_TIMEOUT     = 1.0    # ACK wait timeout

# Max retries
MAX_RETRIES = 10


def crc16_ccitt(data):
    """Calculate CRC-16/CCITT (YModem standard)"""
    crc = 0
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def build_packet(seq, data, packet_size):
    """Build a YModem data packet.

    Args:
        seq: Packet sequence number (1-255, wraps)
        data: Payload data
        packet_size: 128 or 1024

    Returns:
        bytes: Complete packet
    """
    header = bytes([SOH]) if packet_size == PACKET_SIZE_128 else bytes([STX])

    seq_byte = seq & 0xFF
    seq_comp = (~seq_byte) & 0xFF

    # Pad data to fixed size with 0x1A (Ctrl-Z)
    padded = bytearray(data)
    if len(padded) < packet_size:
        padded.extend(b'\x1A' * (packet_size - len(padded)))

    crc = crc16_ccitt(padded)
    crc_bytes = bytes([(crc >> 8) & 0xFF, crc & 0xFF])

    return header + bytes([seq_byte, seq_comp]) + bytes(padded) + crc_bytes


def build_filename_packet(filename, file_size):
    """Build filename packet (SOH 00).

    Format: SOH 00 FF filename\\0 filesize\\0 ... padding 0x00
    """
    info = ("%s\0%d\0" % (filename, file_size)).encode('ascii', errors='replace')

    padded = bytearray(info)
    if len(padded) < PACKET_SIZE_128:
        padded.extend(b'\x00' * (PACKET_SIZE_128 - len(padded)))

    crc = crc16_ccitt(padded)
    crc_bytes = bytes([(crc >> 8) & 0xFF, crc & 0xFF])

    return bytes([SOH, 0x00, 0xFF]) + bytes(padded) + crc_bytes


def build_empty_filename_packet():
    """Build empty filename packet (end of batch)."""
    padded = bytearray(b'\x00' * PACKET_SIZE_128)
    crc = crc16_ccitt(padded)
    crc_bytes = bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    return bytes([SOH, 0x00, 0xFF]) + bytes(padded) + crc_bytes


def read_byte(ser, timeout=1.0):
    """Read a single byte from serial port with timeout."""
    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting > 0:
            return ser.read(1)[0]
        time.sleep(0.001)
    return None


def send_file(ser, filepath, packet_size=PACKET_SIZE_1024, resume_offset=0):
    """Send a file using YModem protocol.

    Returns:
        bool: True on success, False on failure
    """
    filename = os.path.basename(filepath)
    file_size = os.path.getsize(filepath)

    print("")
    print("=" * 60)
    print("  YModem Firmware Sender")
    print("  File: %s" % filename)
    print("  Size: %d bytes (%.1f KB)" % (file_size, file_size / 1024.0))
    print("  Packet size: %d bytes" % packet_size)
    if resume_offset > 0:
        print("  Resume from offset: %d" % resume_offset)
    print("=" * 60)

    # ============================================================
    # Phase 1: Handshake - wait for receiver 'C'
    # ============================================================
    print("\n[1/4] Waiting for receiver 'C' (handshake)...")
    retry = 0
    while retry < MAX_RETRIES:
        b = read_byte(ser, timeout=INIT_TIMEOUT)
        if b == C:
            print("  Received 'C' - handshake OK")
            break
        elif b == NAK:
            retry += 1
            print("  Received NAK, retry %d/%d" % (retry, MAX_RETRIES))
        elif b is None:
            retry += 1
            print("  Timeout waiting for 'C', retry %d/%d" % (retry, MAX_RETRIES))
        else:
            print("  Unexpected byte: 0x%02X" % b)
            retry += 1
    else:
        print("  ERROR: Handshake failed!")
        return False

    # ============================================================
    # Phase 2: Send filename packet
    # ============================================================
    print("\n[2/4] Sending filename packet...")
    retry = 0
    while retry < MAX_RETRIES:
        pkt = build_filename_packet(filename, file_size)
        ser.write(pkt)
        ser.flush()
        print("  Sent filename packet (%d bytes)" % len(pkt))

        # Wait for ACK + 'C'
        b1 = read_byte(ser, timeout=ACK_TIMEOUT)
        if b1 == ACK:
            b2 = read_byte(ser, timeout=ACK_TIMEOUT)
            if b2 == C:
                print("  Received ACK+C - filename accepted")
                break
            else:
                if b2 is not None:
                    print("  Expected 'C' after ACK, got 0x%02X" % b2)
                else:
                    print("  Timeout after ACK")
        elif b1 == NAK:
            retry += 1
            print("  Received NAK, retry %d/%d" % (retry, MAX_RETRIES))
        elif b1 == CAN:
            b2 = read_byte(ser, timeout=0.1)
            if b2 == CAN:
                print("  Received CANCEL")
                return False
        else:
            retry += 1
            if b1 is not None:
                print("  Unexpected: 0x%02X" % b1)
            else:
                print("  Timeout")
    else:
        print("  ERROR: Filename packet rejected!")
        return False

    # ============================================================
    # Phase 3: Send data packets
    # ============================================================
    print("\n[3/4] Sending data packets...")

    with open(filepath, 'rb') as f:
        if resume_offset > 0:
            f.seek(resume_offset)

        seq = 1
        offset = resume_offset
        errors = 0
        t_start = time.time()
        total_data = file_size - resume_offset

        while offset < file_size:
            remaining = file_size - offset
            chunk_size = min(packet_size, remaining)
            chunk = f.read(chunk_size)

            pkt = build_packet(seq, chunk, packet_size)
            ser.write(pkt)
            ser.flush()

            b = read_byte(ser, timeout=PACKET_TIMEOUT)
            if b == ACK:
                errors = 0
                offset += chunk_size
                seq = (seq + 1) & 0xFF
                if seq == 0:
                    seq = 1

                # Progress display
                pct = (offset - resume_offset) * 100 // total_data
                elapsed = time.time() - t_start
                speed = (offset - resume_offset) / elapsed / 1024.0 if elapsed > 0 else 0
                eta = (total_data - (offset - resume_offset)) / (speed * 1024.0) if speed > 0 else 0
                bar_len = 30
                filled = pct * bar_len // 100
                bar = '#' * filled + '-' * (bar_len - filled)
                print("\r  [%s] %3d%% | %d/%d bytes | %.0f KB/s | ETA: %.0fs  " % (
                    bar, pct, offset - resume_offset, total_data, speed, eta),
                    end='', flush=True)
            elif b == NAK:
                errors += 1
                print("\n  NAK for seq=%d, retry %d/%d" % (seq, errors, MAX_RETRIES))
                if errors >= MAX_RETRIES:
                    print("  ERROR: Too many NAKs!")
                    return False
                f.seek(offset)
            elif b == CAN:
                b2 = read_byte(ser, timeout=0.1)
                if b2 == CAN:
                    print("\n  Received CANCEL")
                    return False
            elif b is None:
                errors += 1
                print("\n  Timeout for seq=%d, retry %d/%d" % (seq, errors, MAX_RETRIES))
                if errors >= MAX_RETRIES:
                    print("  ERROR: Too many timeouts!")
                    return False
                f.seek(offset)
            else:
                print("\n  Unexpected: 0x%02X" % b)
                errors += 1
                if errors >= MAX_RETRIES:
                    return False
                f.seek(offset)

        elapsed = time.time() - t_start
        speed = total_data / elapsed / 1024.0 if elapsed > 0 else 0
        print("\n  Done: %d bytes in %.1fs (%.0f KB/s)" % (total_data, elapsed, speed))

    # ============================================================
    # Phase 4: End transfer
    # ============================================================
    print("\n[4/4] Ending transfer...")

    # Send first EOT
    retry = 0
    while retry < MAX_RETRIES:
        ser.write(bytes([EOT]))
        ser.flush()
        b = read_byte(ser, timeout=EOT_TIMEOUT)
        if b == NAK:
            print("  EOT sent, received NAK (expected)")
            break
        elif b == ACK:
            print("  EOT sent, received ACK (continuing)")
            break
        elif b == CAN:
            b2 = read_byte(ser, timeout=0.1)
            if b2 == CAN:
                print("  Received CANCEL")
                return False
        else:
            retry += 1
            print("  EOT retry %d/%d" % (retry, MAX_RETRIES))
    else:
        print("  ERROR: EOT failed!")
        return False

    # Send second EOT
    retry = 0
    while retry < MAX_RETRIES:
        ser.write(bytes([EOT]))
        ser.flush()
        b = read_byte(ser, timeout=EOT_TIMEOUT)
        if b == ACK:
            print("  Second EOT sent, received ACK")
            break
        elif b == NAK:
            retry += 1
            print("  Second EOT NAK, retry %d/%d" % (retry, MAX_RETRIES))
        else:
            retry += 1
    else:
        print("  ERROR: Second EOT failed!")
        return False

    # Wait for 'C' then send empty filename packet
    b = read_byte(ser, timeout=ACK_TIMEOUT)
    if b == C:
        print("  Received 'C', sending empty filename packet...")
        pkt = build_empty_filename_packet()
        ser.write(pkt)
        ser.flush()

        b = read_byte(ser, timeout=ACK_TIMEOUT)
        if b == ACK:
            print("  Received ACK - transfer complete!")
        else:
            if b is not None:
                print("  Expected ACK, got 0x%02X" % b)
            else:
                print("  Timeout")
    else:
        if b is not None:
            print("  Expected 'C', got 0x%02X" % b)
        else:
            print("  Timeout after second EOT")

    print("")
    print("=" * 60)
    print("  YMODEM TRANSFER SUCCESSFUL!")
    print("  File: %s" % filename)
    print("  Size: %d bytes" % file_size)
    print("  Time: %.1fs" % (time.time() - t_start))
    print("=" * 60)

    return True


def list_serial_ports():
    """List available serial ports."""
    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        if not ports:
            print("No serial ports found.")
            return
        print("Available serial ports:")
        for port in sorted(ports):
            print("  %s - %s" % (port.device, port.description))
    except ImportError:
        print("Cannot list ports. Install pyserial: pip install pyserial")


def main():
    parser = argparse.ArgumentParser(
        description='YModem Firmware Sender for UART OTA',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python ymodem_sender.py COM3 firmware.fwpkg
  python ymodem_sender.py COM3 firmware.fwpkg 921600
  python ymodem_sender.py COM3 firmware.fwpkg 115200 -s 1024
  python ymodem_sender.py COM3 firmware.fwpkg 115200 --resume 65536
  python ymodem_sender.py --list
        """
    )
    parser.add_argument('port', nargs='?',
                        help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('firmware', nargs='?',
                        help='Firmware file (.fwpkg or raw binary)')
    parser.add_argument('baudrate', nargs='?', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    parser.add_argument('-s', '--packet-size', type=int,
                        default=PACKET_SIZE_1024,
                        choices=[128, 256, 1024],
                        help='Packet size: 128, 256, or 1024 (default: 1024)')
    parser.add_argument('-r', '--resume', type=int, default=0,
                        help='Resume from offset (bytes)')
    parser.add_argument('--list', action='store_true',
                        help='List available serial ports and exit')

    args = parser.parse_args()

    if args.list:
        list_serial_ports()
        return 0

    if not args.port or not args.firmware:
        parser.print_help()
        return 1

    if not os.path.exists(args.firmware):
        print("ERROR: File not found: %s" % args.firmware)
        return 1

    print("Opening %s @ %d baud..." % (args.port, args.baudrate))
    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=1.0
        )
    except serial.SerialException as e:
        print("ERROR: Cannot open serial port: %s" % e)
        return 1

    print("Opened: %s" % ser.name)

    # Handle Ctrl+C
    def signal_handler(sig, frame):
        print("\n\nAborted by user!")
        ser.write(bytes([CAN, CAN]))
        ser.flush()
        ser.close()
        sys.exit(1)

    signal.signal(signal.SIGINT, signal_handler)

    try:
        success = send_file(ser, args.firmware, args.packet_size, args.resume)
        return 0 if success else 1
    except Exception as e:
        print("\nERROR: %s" % e)
        return 1
    finally:
        ser.close()
        print("Serial port closed.")


if __name__ == '__main__':
    sys.exit(main())
