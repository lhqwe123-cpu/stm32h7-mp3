#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
delta_gen.py - HDiffPatch Delta Firmware Generator

使用 HDiffPatch 的 hdiffz 命令行工具生成差分补丁，
并打包为 .fwpkg 格式供设备端使用。

依赖:
    - hdiffz (HDiffPatch 命令行工具, 需在 PATH 中)
    - fwpkg_pack.py (同目录下的固件打包工具)

用法:
    # 生成差分补丁
    python delta_gen.py diff old_v1.0.bin new_v2.0.bin -b 1.0.0 -n 2.0.0 -o v2.0_delta.fwpkg

    # 查看差分包信息
    python delta_gen.py info v2.0_delta.fwpkg
"""

import sys
import os
import struct
import hashlib
import subprocess
import argparse
import tempfile

# fwpkg 包头格式 (与 C 侧 fwpkg_parser.h 保持一致)
FWPKG_MAGIC = 0x4B505746  # "FWPK" little-endian
FWPKG_HEADER_SIZE = 64
FWPKG_TYPE_FULL = 0x00
FWPKG_TYPE_DELTA = 0x01


def parse_version(version_str: str) -> tuple:
    """解析版本字符串 '1.2.3' 或 '1.2.3.4' 为 (major, minor, rev, build)"""
    parts = version_str.split(".")
    if len(parts) < 3 or len(parts) > 4:
        raise ValueError(f"Invalid version: {version_str}")
    major = int(parts[0])
    minor = int(parts[1])
    rev = int(parts[2])
    build = int(parts[3]) if len(parts) > 3 else 0
    for v in [major, minor, rev, build]:
        if v < 0 or v > 255:
            raise ValueError(f"Version components must be 0-255")
    return (major, minor, rev, build)


def version_to_int(ver: tuple) -> int:
    return (ver[0] << 24) | (ver[1] << 16) | (ver[2] << 8) | ver[3]


def version_to_str(ver: tuple) -> str:
    if ver[3] != 0:
        return f"{ver[0]}.{ver[1]}.{ver[2]}.{ver[3]}"
    return f"{ver[0]}.{ver[1]}.{ver[2]}"


def find_hdiffz():
    """查找 hdiffz 命令行工具"""
    # 先尝试当前目录
    candidates = [
        "hdiffz",
        "./hdiffz",
        "hdiffz.exe",
        os.path.join(os.path.dirname(__file__), "hdiffz"),
    ]
    # 也尝试 HDiffPatch-master 目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    parent = os.path.dirname(script_dir)  # bootloader/
    grandparent = os.path.dirname(parent)  # mp3/
    hdiffpatch_dir = os.path.join(grandparent, "HDiffPatch-master", "builds")
    if os.path.isdir(hdiffpatch_dir):
        for f in os.listdir(hdiffpatch_dir):
            if f.startswith("hdiffz"):
                candidates.append(os.path.join(hdiffpatch_dir, f))

    for c in candidates:
        try:
            result = subprocess.run([c, "-h"], capture_output=True, text=True, timeout=5)
            if result.returncode == 0 or "HDiffPatch" in result.stdout + result.stderr or "hdiffz" in result.stdout + result.stderr:
                return c
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None


def generate_delta(old_bin: str, new_bin: str, output_patch: str, compression: str = "none"):
    """使用 hdiffz 生成差分补丁 (默认不压缩, 嵌入式端无解压库)"""
    hdiffz = find_hdiffz()
    if not hdiffz:
        print("ERROR: hdiffz not found!")
        print("Please build HDiffPatch first:")
        print("  cd HDiffPatch-master && make")
        print("Or download prebuilt from https://github.com/sisong/HDiffPatch/releases")
        sys.exit(1)

    print(f"Using hdiffz: {hdiffz}")

    # hdiffz 命令行: hdiffz [options] oldFile newFile outDiffFile
    # 使用 -SD 模式生成 single compressed diff (HDIFFSF20 格式),
    # 嵌入式端使用 patch_single_stream() 还原, -f 强制覆盖
    cmd = [
        hdiffz,
        "-SD",
        "-f",
        old_bin,
        new_bin,
        output_patch,
    ]

    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"hdiffz error:\n{result.stderr}")
        sys.exit(1)

    print(result.stdout)

    old_size = os.path.getsize(old_bin)
    new_size = os.path.getsize(new_bin)
    patch_size = os.path.getsize(output_patch)

    print(f"\nResults:")
    print(f"  Old firmware: {old_size:,} bytes")
    print(f"  New firmware: {new_size:,} bytes")
    print(f"  Patch size:   {patch_size:,} bytes")
    print(f"  Ratio:        {patch_size / new_size * 100:.1f}%")

    return output_patch


def pack_delta_fwpkg(patch_file: str, base_version: tuple,
                     new_version: tuple, output_fwpkg: str):
    """将差分补丁打包为 .fwpkg 格式"""
    with open(patch_file, 'rb') as f:
        patch_data = f.read()

    # 计算 SHA256
    sha256 = hashlib.sha256(patch_data).digest()

    # 构造包头 (64 bytes)
    # magic(4) + version(4) + image_size(4) + sha256(32) + type(1) + base_version(4) + reserved(15)
    header = struct.pack(
        '<I I I 32s B 4B 15s',
        FWPKG_MAGIC,                    # magic
        version_to_int(new_version),    # version
        len(patch_data),                # image_size
        sha256,                         # sha256
        FWPKG_TYPE_DELTA,               # type = DELTA
        *base_version,                  # base_version (4 bytes)
        b'\x00' * 15                    # reserved
    )

    assert len(header) == FWPKG_HEADER_SIZE, f"Header size: {len(header)}"

    with open(output_fwpkg, 'wb') as f:
        f.write(header)
        f.write(patch_data)

    print(f"\nDelta FWPkg created: {output_fwpkg}")
    print(f"  Header:  {FWPKG_HEADER_SIZE} bytes")
    print(f"  Patch:   {len(patch_data):,} bytes")
    print(f"  Total:   {FWPKG_HEADER_SIZE + len(patch_data):,} bytes")
    print(f"  Type:    DELTA (0x{FWPKG_TYPE_DELTA:02X})")
    print(f"  Base:    v{version_to_str(base_version)}")
    print(f"  Target:  v{version_to_str(new_version)}")
    print(f"  SHA256:  {sha256.hex()}")


def pack_full_fwpkg(image_file: str, version: tuple, output_fwpkg: str):
    """将全量固件打包为 .fwpkg 格式"""
    with open(image_file, 'rb') as f:
        image_data = f.read()

    sha256 = hashlib.sha256(image_data).digest()

    header = struct.pack(
        '<I I I 32s B 4B 15s',
        FWPKG_MAGIC,
        version_to_int(version),
        len(image_data),
        sha256,
        FWPKG_TYPE_FULL,        # type = FULL
        0, 0, 0, 0,             # base_version (unused)
        b'\x00' * 15
    )

    with open(output_fwpkg, 'wb') as f:
        f.write(header)
        f.write(image_data)

    print(f"Full FWPkg created: {output_fwpkg}")
    print(f"  Version: v{version_to_str(version)}")
    print(f"  Image:   {len(image_data):,} bytes")
    print(f"  SHA256:  {sha256.hex()}")


def show_info(fwpkg_path: str):
    """显示 .fwpkg 文件信息"""
    with open(fwpkg_path, 'rb') as f:
        header_data = f.read(FWPKG_HEADER_SIZE)
        if len(header_data) < FWPKG_HEADER_SIZE:
            print("ERROR: File too small")
            return

        magic, ver_int, image_size, sha256, pkg_type, b0, b1, b2, b3, reserved = \
            struct.unpack('<I I I 32s B 4B 15s', header_data)

    if magic != FWPKG_MAGIC:
        print(f"WARNING: Bad magic 0x{magic:08X}, expected 0x{FWPKG_MAGIC:08X}")

    major = (ver_int >> 24) & 0xFF
    minor = (ver_int >> 16) & 0xFF
    rev = (ver_int >> 8) & 0xFF
    build = ver_int & 0xFF

    type_str = "FULL" if pkg_type == FWPKG_TYPE_FULL else \
               "DELTA" if pkg_type == FWPKG_TYPE_DELTA else \
               f"UNKNOWN(0x{pkg_type:02X})"

    print(f"FWPkg Info: {fwpkg_path}")
    print(f"  Magic:      0x{magic:08X}")
    print(f"  Version:    {major}.{minor}.{rev}" + (f".{build}" if build else ""))
    print(f"  Image size: {image_size:,} bytes")
    print(f"  SHA256:     {sha256.hex()}")
    print(f"  Type:       {type_str}")
    if pkg_type == FWPKG_TYPE_DELTA:
        print(f"  Base ver:   {b0}.{b1}.{b2}" + (f".{b3}" if b3 else ""))
    print(f"  Total size: {FWPKG_HEADER_SIZE + image_size:,} bytes")


def main():
    parser = argparse.ArgumentParser(
        description="HDiffPatch Delta Firmware Generator for .fwpkg",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate delta patch
  python delta_gen.py diff v1.0.bin v2.0.bin -b 1.0.0 -n 2.0.0 -o v2.0_delta.fwpkg

  # Generate full package
  python delta_gen.py full v2.0.bin -n 2.0.0 -o v2.0_full.fwpkg

  # Show package info
  python delta_gen.py info firmware.fwpkg
        """
    )

    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # diff 命令
    diff_parser = subparsers.add_parser('diff', help='Generate delta patch')
    diff_parser.add_argument('old_bin', help='Old firmware binary')
    diff_parser.add_argument('new_bin', help='New firmware binary')
    diff_parser.add_argument('-b', '--base-version', required=True,
                             help='Base (old) version, e.g. 1.0.0')
    diff_parser.add_argument('-n', '--new-version', required=True,
                             help='New version, e.g. 2.0.0')
    diff_parser.add_argument('-o', '--output', default=None,
                             help='Output .fwpkg file')
    diff_parser.add_argument('-c', '--compression', default='none',
                             choices=['zlib', 'lzma', 'lzma2', 'none'],
                             help='Compression (default: none, embedded has no decompressor)')

    # full 命令
    full_parser = subparsers.add_parser('full', help='Generate full firmware package')
    full_parser.add_argument('image', help='Firmware binary')
    full_parser.add_argument('-n', '--new-version', required=True,
                             help='Version, e.g. 1.0.0')
    full_parser.add_argument('-o', '--output', default=None,
                             help='Output .fwpkg file')

    # info 命令
    info_parser = subparsers.add_parser('info', help='Show package info')
    info_parser.add_argument('fwpkg', help='.fwpkg file')

    args = parser.parse_args()

    if args.command == 'diff':
        base_ver = parse_version(args.base_version)
        new_ver = parse_version(args.new_version)

        if args.output is None:
            args.output = f"v{version_to_str(new_ver)}_delta.fwpkg"

        # 生成差分补丁
        with tempfile.NamedTemporaryFile(suffix='.patch', delete=False) as tmp:
            patch_tmp = tmp.name

        try:
            generate_delta(args.old_bin, args.new_bin, patch_tmp, args.compression)
            pack_delta_fwpkg(patch_tmp, base_ver, new_ver, args.output)
        finally:
            if os.path.exists(patch_tmp):
                os.remove(patch_tmp)

    elif args.command == 'full':
        new_ver = parse_version(args.new_version)
        if args.output is None:
            args.output = f"v{version_to_str(new_ver)}_full.fwpkg"
        pack_full_fwpkg(args.image, new_ver, args.output)

    elif args.command == 'info':
        show_info(args.fwpkg)

    else:
        parser.print_help()


if __name__ == '__main__':
    main()
