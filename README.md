# NostaluxOS

NostaluxOS is a small x86-64 hobby operating system

This is my first operating-system project, there has been some heavy codex use but I have phased out of it as of 11/22/2025

## Current features

- **Handmade boot flow** – a two-stage loader pulls the 64-bit kernel into memory and switches the CPU into long mode.
- **Text-mode terminal** – the kernel owns the VGA text buffer, tracks the hardware cursor, and lets you change the foreground
  and background colors.
- **Interactive shell** – type `help` to discover commands such as `about`, `clear`, `color`, `history`, `palette`, and `echo`.
  The new `history` command shows the most recent inputs while `palette` lists every VGA color code so you can theme the
  console quickly.

More functionality will follow over time, but these pieces make NostaluxOS fun to poke at immediately after boot.

## Getting started

The repository contains a handcrafted bootloader and a tiny 64-bit kernel. The bootloader loads the kernel into memory,
switches the CPU into long mode, and jumps into the kernel. Once running, the kernel clears the VGA text console, prints a
welcome message, and waits for commands at the `nostalux>` prompt.

### Requirements

The build has been tested with the GNU toolchain on Linux. You will need:

- `nasm`
- `gcc`
- `ld`
- `objcopy`
- `qemu-system-x86_64` (optional, for running the image)

### Build

```sh
make
```

The command produces `build/NostaluxOS.img`, a raw disk image that contains the boot sector, bootloader, and kernel.

### Run in QEMU

```sh
make run
```

The QEMU window will display the boot banner and drop you into the tiny NostaluxOS shell. Type `help` to see the available
commands (such as `about`, `clear`, `color`, `history`, `bg/fg`, or `echo`).

### Cleaning

```sh
make clean
```

This removes all files under the `build/` directory.

## Repository layout

- `bootloader/` — 16-bit MBR loader and 32/64-bit transition stage.
- `kernel/` — 64-bit freestanding kernel sources and linker script.
- `Makefile` — build orchestration that assembles the boot stages, compiles the kernel, and produces a bootable image.

You can play with the kernel, expand the bootloader, or add new features to NostaluxOS!

