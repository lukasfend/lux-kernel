#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-$HOME/opt/cross}"
BIN_DIR="${PREFIX}/bin"

require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: missing required host tool '$1'" >&2
        exit 1
    fi
}

if command -v clang >/dev/null 2>&1; then
    COMPILER_WRAPPER='exec clang --target=i686-unknown-elf "$@"'
else
    require gcc
    COMPILER_WRAPPER='exec gcc -m32 "$@"'
fi

require ld
require objcopy

mkdir -p "$BIN_DIR"

cat >"$BIN_DIR/i686-elf-gcc" <<EOF
#!/usr/bin/env bash
set -euo pipefail
$COMPILER_WRAPPER
EOF

cat >"$BIN_DIR/i686-elf-ld" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exec ld -m elf_i386 "$@"
EOF

cat >"$BIN_DIR/i686-elf-objcopy" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exec objcopy "$@"
EOF

chmod +x "$BIN_DIR/i686-elf-gcc" \
          "$BIN_DIR/i686-elf-ld"  \
          "$BIN_DIR/i686-elf-objcopy"

echo "Installed lightweight wrappers into $BIN_DIR"
