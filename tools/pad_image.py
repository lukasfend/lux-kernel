#!/usr/bin/env python3
import os
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: pad_image.py <file>")

path = pathlib.Path(sys.argv[1])
size = path.stat().st_size
pad = (-size) % 512
if pad:
    with path.open("ab") as fp:
        fp.write(b"\0" * pad)
