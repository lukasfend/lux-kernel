#!/usr/bin/env bash
set -euo pipefail

"$(dirname "$0")/build.sh"
qemu-system-x86_64 -drive format=raw,file=./bin/os.bin