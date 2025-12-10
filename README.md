# lux-kernel

Tiny 32-bit hobby kernel that boots under QEMU/Bochs/real hardware. The boot sector loads a protected-mode kernel from disk, jumps into a freestanding C entry point, and writes to VGA text memory.
It now includes a polled PS/2 keyboard driver and a minimal interactive shell.

## Prerequisites

- `nasm`
- `python3`
- `qemu-system-x86_64` (or another emulator)
- Host `gcc`/`ld`/`objcopy` (used to create lightweight `i686-elf-*` wrappers)

Run once to provision the local cross tools inside `~/opt/cross/bin`:

```bash
./tools/install_local_toolchain.sh
```

## Building

```bash
make            # or ./build.sh
```

The build pipeline:

1. Installs/refreshes the lightweight `i686-elf-*` wrappers (when using `build.sh`).
2. Compiles the kernel entry stub plus all C sources in `src/`.
3. Links them at physical address `0x0010000` using `src/linker.ld`.
4. Converts the ELF to a flat binary and calculates the required sector count.
5. Re-assembles `src/boot/boot.asm`, which now knows how many sectors to load via BIOS LBA (supports contiguous kernels up to ~960 KiB).
6. Concatenates and pads the boot sector + kernel into `bin/os.bin`.

## Running

```bash
make run        # builds then boots with QEMU
```

Or run `./run.sh`.

Once the prompt `lux>` appears you can type commands on the emulated PS/2 keyboard.

## Built-in shell commands

| Command | Description |
| ------- | ----------- |
| `help`  | List all built-in commands |
| `echo`  | Print its arguments back to the console |

Use these as templates when adding more functionality inside `src/shell.c`.

## Extending with C code

- Drop new `.c` files anywhere under `src/`; the Makefile automatically mirrors them into `build/`.
- Use the tiny freestanding runtime in `src/lib/` (`memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, VGA TTY helpers, keyboard polling, port I/O shims) as a starting point.
- Add your own headers under `src/include/` and include them with `#include <your_header.h>`.
- The bootloader loads the kernel to physical `0x0010000` and can stream any contiguous image that fits below the 1 MiB real-mode ceiling. Grow bigger by enabling paging or relocating the load target after switching to protected mode.
