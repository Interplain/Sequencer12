#!/usr/bin/env python3
"""Host-side verification for S12 MB85RC256 FRAM backup dumps.

Read-only tool: never modifies the .bin files it inspects.

Usage (single dump):
    python3 fram_backup_verify.py --dump backup_A.bin --status backup_A_status.bin

Usage (two independent dumps, verified byte-for-byte identical):
    python3 fram_backup_verify.py \\
        --dump backup_A.bin --status backup_A_status.bin \\
        --dump2 backup_B.bin --status2 backup_B_status.bin

`--status` is the 10-byte OpenOCD dump of the firmware's backup status globals
(g_fram_backup_bytes_read, g_fram_backup_crc32, g_fram_backup_done,
g_fram_backup_ok), captured contiguously, e.g.:

    dump_image backup_A_status.bin 0x<addr_of_bytes_read> 10

Region offsets below mirror platform/fram/fram_layout.h and must be kept in
sync if that header changes.
"""

import argparse
import struct
import sys
import zlib

FRAM_SIZE = 0x8000

CHORD_MAGIC_ADDR = 0x0000
CHORD_MAGIC_SIZE = 4
CHORD_MAGIC_EXPECTED = bytes([0xC4, 0x0D, 0x55, 0x01])

CAL_ADDR = 0x0004
CAL_SIZE = 28
CAL_MAGIC_EXPECTED = 0x53314341
CAL_VERSION_EXPECTED = 4

USERCHORDS_ADDR = 0x0020
USERCHORDS_SLOT_SIZE = 22
USERCHORDS_SLOT_COUNT = 128
USERCHORDS_SIZE = USERCHORDS_SLOT_SIZE * USERCHORDS_SLOT_COUNT
USERCHORDS_INUSE_OFFSET = 20  # byte offset of `in_use` within each 22-byte slot

SONGDATA_ADDR = USERCHORDS_ADDR + USERCHORDS_SIZE  # 0x0B20
SETTINGS_SIZE = 0x0100
FUTURE_SIZE = 0x0100
SETTINGS_ADDR = FRAM_SIZE - SETTINGS_SIZE - FUTURE_SIZE  # 0x7E00
FUTURE_ADDR = FRAM_SIZE - FUTURE_SIZE                    # 0x7F00
SONGDATA_SIZE = SETTINGS_ADDR - SONGDATA_ADDR

SONG_MAGIC_EXPECTED = b"S12S"

SETTINGS_MAGIC_EXPECTED = 0x53313253
CALIB_SAVECOUNT_ADDR = FUTURE_ADDR


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")


def ok(msg: str) -> None:
    print(f"OK:   {msg}")


def load_dump(path: str) -> bytes:
    with open(path, "rb") as f:
        data = f.read()
    return data


def check_size(data: bytes, label: str) -> bool:
    if len(data) != FRAM_SIZE:
        fail(f"{label}: size {len(data)} != {FRAM_SIZE}")
        return False
    ok(f"{label}: size == {FRAM_SIZE} bytes")
    return True


def parse_status(path: str):
    """10-byte layout: bytes_read(u32 LE), crc32(u32 LE), done(u8), ok(u8)."""
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) != 10:
        fail(f"{path}: status file size {len(raw)} != 10")
        return None
    bytes_read, crc32, done, is_ok = struct.unpack("<IIBB", raw)
    return {"bytes_read": bytes_read, "crc32": crc32, "done": done, "ok": is_ok}


def check_crc(data: bytes, status: dict, label: str) -> bool:
    host_crc = zlib.crc32(data) & 0xFFFFFFFF
    good = True

    if status["bytes_read"] != FRAM_SIZE:
        fail(f"{label}: firmware bytes_read {status['bytes_read']} != {FRAM_SIZE}")
        good = False
    else:
        ok(f"{label}: firmware bytes_read == {FRAM_SIZE}")

    if not status["done"] or not status["ok"]:
        fail(f"{label}: firmware done={status['done']} ok={status['ok']} (expected 1/1)")
        good = False
    else:
        ok(f"{label}: firmware reported done=1 ok=1")

    if host_crc != status["crc32"]:
        fail(f"{label}: host CRC32 0x{host_crc:08X} != firmware CRC32 0x{status['crc32']:08X}")
        good = False
    else:
        ok(f"{label}: host CRC32 0x{host_crc:08X} == firmware CRC32 0x{status['crc32']:08X}")

    return good


def decode_regions(data: bytes) -> None:
    print("\n--- Decoded regions (read-only) ---")

    magic = data[CHORD_MAGIC_ADDR:CHORD_MAGIC_ADDR + CHORD_MAGIC_SIZE]
    present = magic == CHORD_MAGIC_EXPECTED
    print(f"[0x0000] user-chord magic: {magic.hex()} "
          f"({'present/valid' if present else 'not set (no chords saved yet)'})")

    cal = data[CAL_ADDR:CAL_ADDR + CAL_SIZE]
    cal_magic, cal_version, ch_a, ch_b, ch_c, ch_d, cal_checksum = struct.unpack("<IIIIIII", cal)
    cal_valid_hdr = (cal_magic == CAL_MAGIC_EXPECTED and cal_version == CAL_VERSION_EXPECTED)
    computed_checksum = (cal_magic ^ cal_version ^ ch_a ^ ch_b ^ ch_c ^ ch_d ^ 0xA5A55A5A) & 0xFFFFFFFF
    checksum_ok = computed_checksum == cal_checksum
    print(f"[0x0004] calibration: magic=0x{cal_magic:08X} version={cal_version} "
          f"header_valid={cal_valid_hdr} checksum_valid={checksum_ok}")
    for name, packed in (("A", ch_a), ("B", ch_b), ("C", ch_c), ("D", ch_d)):
        neg1 = packed & 0xFFFF
        pos2 = (packed >> 16) & 0xFFFF
        print(f"         ch_{name}: neg1_code={neg1} pos2_code={pos2}")

    used = 0
    for i in range(USERCHORDS_SLOT_COUNT):
        off = USERCHORDS_ADDR + i * USERCHORDS_SLOT_SIZE
        in_use = data[off + USERCHORDS_INUSE_OFFSET]
        if in_use:
            used += 1
    print(f"[0x0020] user-chord table: {used}/{USERCHORDS_SLOT_COUNT} slots in_use "
          f"(magic {'valid' if present else 'absent'} so table is "
          f"{'meaningful' if present else 'presumed unwritten'})")

    song = data[SONGDATA_ADDR:SONGDATA_ADDR + 16]
    song_magic, song_version, song_payload_size, song_checksum = struct.unpack("<4sIII", song)
    song_magic_ok = song_magic == SONG_MAGIC_EXPECTED
    print(f"[0x{SONGDATA_ADDR:04X}] song header: magic={song_magic!r} "
          f"({'valid' if song_magic_ok else 'invalid/absent'}) version={song_version} "
          f"payload_size={song_payload_size} checksum=0x{song_checksum:08X} "
          f"(region size 0x{SONGDATA_SIZE:04X})")

    settings = data[SETTINGS_ADDR:SETTINGS_ADDR + 16]
    set_magic, set_version, set_payload_size, set_checksum, set_flags = struct.unpack("<IHHII", settings)
    set_magic_ok = set_magic == SETTINGS_MAGIC_EXPECTED
    print(f"[0x{SETTINGS_ADDR:04X}] settings blob: magic=0x{set_magic:08X} "
          f"({'valid' if set_magic_ok else 'invalid/absent'}) version={set_version} "
          f"payload_size={set_payload_size} checksum=0x{set_checksum:08X} flags=0x{set_flags:08X}")

    (savecount,) = struct.unpack("<I", data[CALIB_SAVECOUNT_ADDR:CALIB_SAVECOUNT_ADDR + 4])
    print(f"[0x{CALIB_SAVECOUNT_ADDR:04X}] calibration save-counter: {savecount}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", required=True, help="Path to first 32768-byte backup .bin")
    parser.add_argument("--status", required=True, help="Path to first 10-byte status dump")
    parser.add_argument("--dump2", help="Path to second 32768-byte backup .bin (optional)")
    parser.add_argument("--status2", help="Path to second 10-byte status dump (optional)")
    args = parser.parse_args()

    all_good = True

    data_a = load_dump(args.dump)
    all_good &= check_size(data_a, args.dump)
    status_a = parse_status(args.status)
    if status_a is None:
        return 1
    all_good &= check_crc(data_a, status_a, args.dump)
    decode_regions(data_a)

    if args.dump2:
        if not args.status2:
            fail("--dump2 given without --status2")
            return 1
        print("\n=== Second dump ===")
        data_b = load_dump(args.dump2)
        all_good &= check_size(data_b, args.dump2)
        status_b = parse_status(args.status2)
        if status_b is None:
            return 1
        all_good &= check_crc(data_b, status_b, args.dump2)
        decode_regions(data_b)

        print("\n--- Cross-check ---")
        if data_a == data_b:
            ok(f"{args.dump} and {args.dump2} are byte-for-byte identical")
        else:
            fail(f"{args.dump} and {args.dump2} DIFFER")
            all_good = False

    print("\n=== RESULT:", "PASS" if all_good else "FAIL", "===")
    return 0 if all_good else 1


if __name__ == "__main__":
    sys.exit(main())
