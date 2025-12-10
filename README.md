# lux-kernel

A tiny 32-bit hobby operating system that boots under QEMU. It ships with a BIOS boot sector, a protected-mode kernel written in freestanding C, VGA and PS/2 drivers, a persistent ATA-backed filesystem, and an interactive shell to explore the system.

---

## 1. Overview

lux-kernel keeps the learning curve gentle while still exercising every part of the boot pipeline. The current target is i386 protected mode with flat paging disabled, so every component can be inspected, single-stepped, and rebuilt quickly.

### Boot pipeline
- src/arch/x86/boot/boot.asm: BIOS stage, A20 enable, disk load.
- src/arch/x86/kernel/entry.asm: GDT setup, stack init, C entry handoff.
- src/arch/x86/linker.ld: Places the kernel at physical 0x0010000 and ensures sections stay within 1 MiB so the boot sector can load them safely.

### Kernel services
- Core: src/kernel/core/kernel.c wires up drivers and starts the shell.
- Drivers: VGA text console, PS/2 keyboard (Set 1), ATA PIO LBA28 storage.
- Filesystem: 2 MiB Unix-like volume starting at LBA 2048 inside bin/os.bin.
- Runtime: minimal libc-style helpers in src/kernel/lib/.
- Shell: command registry, REPL, and piping helpers in src/kernel/shell/.

## 2. Feature Highlights
- Fully freestanding build (no host libc, no BIOS calls after entry).
- Deterministic ATA-backed filesystem that persists across QEMU runs.
- Extensible shell with pluggable commands and simple piping contract.
- Lightweight local cross toolchain wrappers for repeatable builds.
- Minimal codebase focused on clarity—perfect for stepping through OS fundamentals.

## 3. Architecture at a Glance
| Layer | Responsibility |
| ----- | -------------- |
| Boot sector | BIOS entry point, loads the kernel image, switches to protected mode. |
| Entry stub | Establishes flat segmentation, stack, and jumps into kmain. |
| Core (src/kernel/core/) | Initializes subsystems, mounts the filesystem, starts the shell. |
| Drivers (src/kernel/drivers/) | Video (TTY + font data), input (PS/2 keyboard), storage (ATA PIO). |
| Library (src/kernel/lib/) | mem*, str*, printf, malloc, div64, time helpers. |
| Shell (src/kernel/shell/) | Built-in command registry, REPL, and command I/O glue. |

Memory remains identity-mapped; interrupts stay disabled until an IDT gets added. This keeps debugging painless while leaving room for advanced work (paging, PIC remap, etc.).

## 4. Prerequisites

You need the following host tools:

| Tool | Why |
| ---- | --- |
| git | clone the repo |
| nasm | assemble boot sector + entry stub |
| python3 | helper scripts (padding, sector count) |
| qemu-system-x86_64 | run the OS in an emulator |
| gcc, ld, objcopy | used to create light i686 cross wrappers |

### Install the lightweight cross compiler wrappers (once)

```
./tools/install_local_toolchain.sh
```

The script places tiny wrappers (i686-elf-gcc, etc.) in ~/opt/cross/bin (override via PREFIX). make prepends that directory to PATH so subsequent builds pick up the freestanding flags automatically.

## 5. Build & Run

1. Install the toolchain wrappers (once): ./tools/install_local_toolchain.sh.
2. Build everything: make (artifacts land in bin/).
3. Launch QEMU: make run or ./run.sh.

```
make            # builds bin/os.bin
make clean      # purge build/ + bin/
make run        # boots the freshly built image in QEMU
```

During a successful boot you should see:

```
lux-kernel
Runtime primitives online.
Type 'help' for a list of commands.
lux>
```

Artifacts of interest:
- bin/boot.bin, bin/kernel.bin, bin/os.bin: boot sector, flat kernel, and combined disk image.
- build/kernel_sectors.inc: auto-generated constant consumed by the boot sector.

## 6. Shell Reference

The shell understands simple pipelines (cmd1 | cmd2) and whitespace-separated arguments. Each command is described below; use help cmd inside QEMU for inline docs.

| Command | Arguments | Description |
| ------- | --------- | ----------- |
| help [cmd] | Optional command name | Lists available commands or prints per-command help. |
| echo <text> | Any text | Prints the provided arguments verbatim. |
| printf <fmt> [args...] | Format string + args | Minimal printf supporting %s, %x, %u, %d. |
| clear | none | Clears the VGA text buffer and redraws the prompt. |
| ls [path] | Optional absolute path | Lists directory contents (defaults to /). |
| cat <path...> | One or more file paths | Dumps file contents to stdout; multiple files are concatenated. |
| touch <path> | Absolute file path | Creates or overwrites a file. If data is piped in, it becomes the file body. |
| mkdir <path> | Absolute directory path | Creates a directory; parent directories must exist. |
| hexdump <path> | File path | Emits a hex view with offsets for quick inspection. |
| meminfo | none | Reports heap usage, stack top, and free memory estimates. |
| sleep <ticks> | Integer ticks | Busy-waits for the requested timer ticks (approximate milliseconds). |
| noise | none | Emits pseudo-random characters; handy for stress-testing the TTY scrollback. |
| shutdown | none | Halts the CPU so QEMU exits. |

Tips:
- Use host piping to seed files: echo hi | ./run.sh then touch /hi.txt inside QEMU with stdin redirected.
- Stack another command after a writer: printf foo | cat /dev/stdin (stdin is abstracted by shell_io_*).

## 7. Directory Layout

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

## 8. Development Guide

### Add a new driver or subsystem
1. Drop a .c file under an appropriate folder in src/kernel/ (create subfolders as needed).
2. Declare public interfaces in src/include/lux/your_header.h.
3. Include them from clients via #include <lux/your_header.h>.
4. Build with make; new files are auto-discovered by wildcard rules.

### Add shell commands
1. Place a new file in src/kernel/shell/commands/ that defines a const struct shell_command.
2. Declare the symbol in src/kernel/shell/command_table.c and append it to the builtin[] array.
3. Use shell_io_read() / shell_io_write() helpers to integrate with pipelines.

### Filesystem and storage tips
- The filesystem spans 2 MiB starting at LBA 2048 inside bin/os.bin (after the boot + kernel image).
- The first mount formats the region if no superblock exists, so deleting bin/os.bin gives you a clean slate.
- ATA I/O is synchronous and polled; keep requests under 128 sectors to match the simple driver assumptions.

### Rapid iteration
- make qemu-gdb (if you add such a target) can expose a GDB stub; by default you can attach with gdb -ex "target remote localhost:1234" -quiet bin/kernel.elf after editing the Makefile.
- Use build/ artifacts to inspect intermediate ELF objects, disassembly, or map files.

## 9. Help & Troubleshooting

| Symptom | Fix |
| ------- | --- |
| i686-elf-gcc: No such file or directory | Run ./tools/install_local_toolchain.sh; confirm ~/opt/cross/bin is on PATH. |
| Blank QEMU window that instantly resets | Ensure interrupts remain disabled until you install an IDT; stray sti will triple fault. |
| Keyboard input ignored | Click the QEMU window, ensure Caps/Num Lock are off; only Set 1 scancodes are implemented. |
| Corrupted filesystem data | Delete bin/os.bin to rebuild the disk image or implement fsck style tooling. |
| Build fails with nasm: command not found | Install NASM via your distro package manager (e.g., sudo apt install nasm). |

Need a fresh disk? Remove bin/os.bin (or run make clobber if you add it) and rebuild; the first boot will format the filesystem again.

## 10. Next Steps

- Implement an IDT + PIC remap so you can enable hardware interrupts safely.
- Add paging (identity-map the first few MiB, then remap the kernel higher).
- Write a physical/virtual memory allocator and expose libc-like helpers.
- Replace the polled keyboard with interrupt-driven input.
- Expand the filesystem (subdirectories, deletion, caching) and grow the shell with commands like rm, cp, and redirection.

## 11. TODOS
- Fix shell interrupts being handled in the command executors; move logic into a global handler.
- Optimize for 640x480 output with at least 16 colors for better visual fidelity.
