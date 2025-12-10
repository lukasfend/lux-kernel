#!/usr/bin/env bash
set -euo pipefail

export PREFIX="${PREFIX:-$HOME/opt/cross}"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

"$(dirname "$0")/tools/install_local_toolchain.sh"
make all