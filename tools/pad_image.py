#!/usr/bin/env python3
import pathlib
import sys

SECTOR_SIZE = 512
LUXFS_START_LBA = 2048
LUXFS_TOTAL_SECTORS = 4096
MIN_DISK_SECTORS = LUXFS_START_LBA + LUXFS_TOTAL_SECTORS
MIN_DISK_SIZE = MIN_DISK_SECTORS * SECTOR_SIZE

if len(sys.argv) != 2:
    raise SystemExit("usage: pad_image.py <file>")

path = pathlib.Path(sys.argv[1])
size = path.stat().st_size

target = size
if target < MIN_DISK_SIZE:
    target = MIN_DISK_SIZE

remainder = target % SECTOR_SIZE
if remainder:
    target += SECTOR_SIZE - remainder

pad = target - size
if pad > 0:
    with path.open("ab") as fp:
        fp.write(b"\0" * pad)
