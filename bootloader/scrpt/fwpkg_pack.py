#!/usr/bin/env python3
"""
fwpkg_pack.py - MCUboot Firmware Package (.fwpkg) Packing Tool

Usage:
    python3 fwpkg_pack.py <signed_image.bin> <version> [output.fwpkg]

Example:
    python3 fwpkg_pack.py zephyr.signed.bin 1.0.0 firmware_v1.0.0.fwpkg

.fwpkg File Format:
    [Header: 64 bytes]
        magic       : 4 bytes  "FWPK" (0x4B505746 little-endian)
        version     : 4 bytes  major<<24 | minor<<16 | revision<<8 | build
        image_size  : 4 bytes  firmware image size in bytes
        image_hash  : 32 bytes SHA256 hash of the firmware image
        reserved    : 20 bytes (zero-filled)
    [Firmware Image: variable]
        Raw signed MCUboot image binary
"""

import sys
import os
import struct
import hashlib

FWPKG_MAGIC = 0x4B505746  # "FWPK" little-endian
FWPKG_HEADER_SIZE = 64
FWPKG_TYPE_FULL = 0x00
FWPKG_TYPE_DELTA = 0x01


def parse_version(version_str: str) -> int:
    """Parse version string like '1.2.3' or '1.2.3.456' into 32-bit integer."""
    parts = version_str.split(".")
    if len(parts) < 3 or len(parts) > 4:
        raise ValueError(f"Invalid version format: {version_str}. "
                         f"Expected: major.minor.revision[.build]")

    major = int(parts[0])
    minor = int(parts[1])
    revision = int(parts[2])
    build = int(parts[3]) if len(parts) > 3 else 0

    if major > 255 or minor > 255 or revision > 255 or build > 255:
        raise ValueError("Version components must be 0-255")

    return (major << 24) | (minor << 16) | (revision << 8) | build


def version_to_str(version: int) -> str:
    """Convert 32-bit version integer to string."""
    major = (version >> 24) & 0xFF
    minor = (version >> 16) & 0xFF
    revision = (version >> 8) & 0xFF
    build = version & 0xFF
    if build != 0:
        return f"{major}.{minor}.{revision}.{build}"
    return f"{major}.{minor}.{revision}"


def pack_fwpkg(image_path: str, version: int, output_path: str) -> None:
    """Pack a signed MCUboot image into .fwpkg format."""

    # Read the firmware image
    with open(image_path, "rb") as f:
        image_data = f.read()

    image_size = len(image_data)
    if image_size == 0:
        raise ValueError("Firmware image is empty")

    # Calculate SHA256 hash of the image
    image_hash = hashlib.sha256(image_data).digest()

    # Build header (new 64-byte format with type field)
    header = struct.pack(
        "<I I I 32s B 4B 15s",
        FWPKG_MAGIC,        # magic
        version,            # version
        image_size,         # image_size
        image_hash,         # image_hash (32 bytes)
        FWPKG_TYPE_FULL,    # type = FULL
        0, 0, 0, 0,         # base_version (unused for full)
        b'\x00' * 15,       # reserved (15 bytes)
    )

    assert len(header) == FWPKG_HEADER_SIZE, \
        f"Header size mismatch: {len(header)} != {FWPKG_HEADER_SIZE}"

    # Write .fwpkg file
    with open(output_path, "wb") as f:
        f.write(header)
        f.write(image_data)

    print(f"Successfully packed firmware package:")
    print(f"  Input:   {image_path}")
    print(f"  Output:  {output_path}")
    print(f"  Version: {version_to_str(version)}")
    print(f"  Image:   {image_size} bytes")
    print(f"  SHA256:  {image_hash.hex()}")
    print(f"  Total:   {FWPKG_HEADER_SIZE + image_size} bytes")


def unpack_fwpkg(fwpkg_path: str, output_image_path: str) -> None:
    """Unpack a .fwpkg file back to raw signed image."""

    with open(fwpkg_path, "rb") as f:
        header_data = f.read(FWPKG_HEADER_SIZE)
        if len(header_data) < FWPKG_HEADER_SIZE:
            raise ValueError("File too small to be a valid .fwpkg")

        magic, version, image_size, image_hash, pkg_type, b0, b1, b2, b3, _ = struct.unpack(
            "<I I I 32s B 4B 15s", header_data
        )

        if magic != FWPKG_MAGIC:
            raise ValueError(f"Invalid magic: 0x{magic:08X} (expected 0x{FWPKG_MAGIC:08X})")

        image_data = f.read()

    if len(image_data) != image_size:
        print(f"Warning: declared size {image_size}, actual {len(image_data)}")

    # Verify hash
    computed_hash = hashlib.sha256(image_data).digest()
    if computed_hash != image_hash:
        print(f"Warning: SHA256 mismatch!")
        print(f"  Stored:   {image_hash.hex()}")
        print(f"  Computed: {computed_hash.hex()}")
    else:
        print("SHA256 verification: OK")

    with open(output_image_path, "wb") as f:
        f.write(image_data)

    print(f"Successfully unpacked firmware package:")
    print(f"  Input:   {fwpkg_path}")
    print(f"  Output:  {output_image_path}")
    print(f"  Version: {version_to_str(version)}")
    print(f"  Image:   {len(image_data)} bytes")


def print_usage():
    print(__doc__)


def main():
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "pack":
        if len(sys.argv) < 4:
            print("Usage: fwpkg_pack.py pack <signed_image.bin> <version> [output.fwpkg]")
            sys.exit(1)

        image_path = sys.argv[2]
        version_str = sys.argv[3]

        if len(sys.argv) >= 5:
            output_path = sys.argv[4]
        else:
            base = os.path.splitext(os.path.basename(image_path))[0]
            output_path = f"{base}_v{version_str}.fwpkg"

        try:
            version = parse_version(version_str)
            pack_fwpkg(image_path, version, output_path)
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

    elif cmd == "unpack":
        if len(sys.argv) < 4:
            print("Usage: fwpkg_pack.py unpack <input.fwpkg> <output_image.bin>")
            sys.exit(1)

        fwpkg_path = sys.argv[2]
        output_path = sys.argv[3]

        try:
            unpack_fwpkg(fwpkg_path, output_path)
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

    elif cmd == "info":
        if len(sys.argv) < 3:
            print("Usage: fwpkg_pack.py info <input.fwpkg>")
            sys.exit(1)

        fwpkg_path = sys.argv[2]
        try:
            with open(fwpkg_path, "rb") as f:
                header_data = f.read(FWPKG_HEADER_SIZE)
                if len(header_data) < FWPKG_HEADER_SIZE:
                    raise ValueError("File too small")

                magic, version, image_size, image_hash, pkg_type, b0, b1, b2, b3, _ = struct.unpack(
                    "<I I I 32s B 4B 15s", header_data
                )

                if magic != FWPKG_MAGIC:
                    raise ValueError(f"Invalid magic: 0x{magic:08X}")

                file_size = os.path.getsize(fwpkg_path)

                print(f"Firmware Package Info: {fwpkg_path}")
                print(f"  Magic:      0x{magic:08X} (valid)")
                print(f"  Version:    {version_to_str(version)}")
                print(f"  Image Size: {image_size} bytes")
                print(f"  SHA256:     {image_hash.hex()}")
                print(f"  File Size:  {file_size} bytes")
                print(f"  Header:     {FWPKG_HEADER_SIZE} bytes")
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

    else:
        print(f"Unknown command: {cmd}")
        print_usage()
        sys.exit(1)


if __name__ == "__main__":
    main()
