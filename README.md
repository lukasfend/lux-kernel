# lux-kernel

A tiny 32-bit hobby operating system that boots under QEMU. It contains a BIOS boot sector, a protected-mode kernel written in freestanding C, a VGA text driver, a PS/2 keyboard driver, and a minimal interactive shell.


---

## 1. What You Get

- **Boot sector** (`src/arch/x86/boot/boot.asm`): Loads the kernel from disk at 0x0010000, enables A20, switches to protected mode.
- **Kernel entry stub** (`src/arch/x86/kernel/entry.asm`): Sets up segment registers + stack, then calls the C kernel.
- **Kernel core** (`src/kernel/core/kernel.c`): Initializes drivers and starts the shell.
- **Drivers**
  - `src/kernel/drivers/video/tty.c`: VGA text console with scrolling/backspace + hardware cursor updates.
  - `src/kernel/drivers/input/keyboard.c`: Polled PS/2 keyboard reader (Set 1 scancodes).
- **C runtime helpers** (`src/kernel/lib/string.c`): Basic `mem*`, `str*`, `strcmp`.
- **Shell** (`src/kernel/shell/shell.c`): Prompt, line editing, built-in `help` and `echo` commands.
- **Headers** (`src/include/`): Minimal replacements for `stddef.h`, `stdint.h`, etc., plus OS-specific headers in `src/include/lux/`.

---

## 2. Prerequisites

You need the following host tools:

| Tool | Why |
| ---- | --- |
| `git` | clone the repo |
| `nasm` | assemble boot sector + entry stub |
| `python3` | helper scripts (padding, sector count) |
| `qemu-system-x86_64` | run the OS in an emulator |
| `gcc`, `ld`, `objcopy` | used to create light i686 cross wrappers |

### Install the lightweight cross compiler wrappers (once)

```bash
./tools/install_local_toolchain.sh
```

This drops tiny wrapper scripts (`i686-elf-gcc`, etc.) into `~/opt/cross/bin` (override via `PREFIX`). The build automatically prepends that directory to `PATH`.

---

## 3. Directory Layout

```
.
├── src/
│   ├── arch/x86/
│   │   ├── boot/boot.asm      # BIOS stage
│   │   ├── kernel/entry.asm   # Protected-mode entry
│   │   └── linker.ld          # i386 ELF layout
│   ├── kernel/core/           # kmain and high-level logic
│   ├── kernel/drivers/
│   │   ├── input/keyboard.c
│   │   └── video/tty.c
│   ├── kernel/lib/            # freestanding runtime
│   └── kernel/shell/          # command registry + REPL
├── src/include/
│   ├── stddef.h stdint.h stdbool.h string.h
│   └── lux/
│       ├── io.h keyboard.h shell.h tty.h
├── tools/                     # helper scripts (padding, toolchain)
├── build/                     # obj files (ignored)
└── bin/                       # boot.bin, kernel.bin, os.bin (ignored)
```

---

## 4. Building the OS

```bash
make            # default target builds bin/os.bin
# or, to rebuild everything clean:
make clean && make
```

What happens under the hood:

1. Each C file under `src/kernel/` is compiled with freestanding flags (`-ffreestanding`, `-nostdlib`, etc.) and includes from `src/include/`.
2. The architecture entry stub is assembled to an ELF object.
3. Everything links at physical address `0x0010000` via `src/arch/x86/linker.ld`.
4. `objcopy` converts the ELF to a flat binary, and a Python helper calculates how many 512-byte sectors it occupies.
5. The boot sector is reassembled with that `KERNEL_SECTORS` constant and concatenated with the kernel image.
6. `tools/pad_image.py` pads `bin/os.bin` to the next 512-byte boundary so BIOS reads never run past the file.

---

## 5. Running in QEMU

```bash
make run
# or, equivalently
./run.sh
```

You should see:

```
lux-kernel
Runtime primitives online.
Type 'help' for a list of commands.
lux>
```

Type on your real keyboard; QEMU forwards input to the emulated PS/2 device. Try:

- `help` – lists commands
- `echo hello world` – prints the arguments back

Press `Ctrl+C` in the host terminal to stop QEMU.

---

## 6. Extending the Kernel

### Add a new driver or subsystem
1. Drop a `.c` file under an appropriate folder in `src/kernel/` (create subfolders as needed).
2. Declare public interfaces in `src/include/lux/your_header.h`.
3. Include them as `#include <lux/your_header.h>`.
4. `make` automatically compiles the file—no need to edit the Makefile.

### Add shell commands
- Edit `src/kernel/shell/shell.c`:
  1. Write a handler function (`static void cmd_name(int argc, char **argv)`).
  2. Append an entry to the `commands[]` table.
  3. Rebuild. The new command appears in `help` automatically.

### Support other architectures
- Add another directory under `src/arch/<arch-name>/` with its own bootloader, entry stub, and linker script.
- Build with `make ARCH=<arch-name>` (once you add the necessary files).

---

## 7. Troubleshooting

| Symptom | Fix |
| ------- | --- |
| `i686-elf-gcc: No such file or directory` | Run `./tools/install_local_toolchain.sh`; ensure `PREFIX/bin` is on `PATH`. |
| Blank QEMU window that instantly resets | Ensure interrupts remain disabled until you install an IDT (current code leaves them off). |
| Keyboard does nothing | Make sure the QEMU window is focused; this driver only supports Set 1 scancodes and ignores modifier keys. |
| Build fails with NASM not found | Install `nasm` via your package manager. |

---

## 8. Next Steps

- Implement an IDT + PIC remap so you can enable hardware interrupts safely.
- Add paging (identity-map first megabytes, then map kernel higher).
- Write a basic memory allocator and expose libc-like helpers.
- Replace the polled keyboard with interrupt-driven input.
- Expand the shell with filesystem-style commands once storage drivers exist.

Have fun hacking on lux-kernel! If you get stuck, re-read the sections above—they walk through every moving part of the current system.

## TODOS:
* Fix shell interrupts being handled in he command executors - use a global system instead
* Optimize for usage visually - using a 640x480 display with at least 16 colors.