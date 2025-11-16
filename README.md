# MemoriaOS

MemoriaOS is a deliberately small x86-64 hobby operating system with a nostalgic spin: it boots straight to a VGA text console,
shows a retro banner, and drops you into a built-in shell. The goal is to keep the code approachable while capturing the charm of
classic PCs.

This is my first operating-system project, so the repository doubles as a sandbox for experimenting with bootloaders, kernel
structure, and user interaction.

## Current features

- **Handmade boot flow** – a two-stage loader pulls the 64-bit kernel into memory and switches the CPU into long mode.
- **Text-mode terminal** – the kernel owns the VGA text buffer, tracks the hardware cursor, and lets you change the foreground
  and background colors.
- **Interactive shell** – type `help` to discover commands such as `about`, `clear`, `color`, `history`, `palette`, and `echo`.
  The new `history` command shows the most recent inputs while `palette` lists every VGA color code so you can theme the
  console quickly.

More functionality will follow over time, but these pieces make MemoriaOS fun to poke at immediately after boot.

## Getting started

The repository contains a handcrafted bootloader and a tiny 64-bit kernel. The bootloader loads the kernel into memory,
switches the CPU into long mode, and jumps into the kernel. Once running, the kernel clears the VGA text console, prints a
welcome message, and waits for commands at the `memoria>` prompt.

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

The command produces `build/MemoriaOS.img`, a raw disk image that contains the boot sector, bootloader, and kernel.

### Run in QEMU

```sh
make run
```

The QEMU window will display the boot banner and drop you into the tiny MemoriaOS shell. Type `help` to see the available
commands (such as `about`, `clear`, `color`, `history`, `palette`, or `echo`).
The QEMU window will display the boot banner and drop you into the tiny RetroOS shell. Type `help` to see the available
commands (such as `about`, `clear`, `color`, or `echo`).

### Cleaning

```sh
make clean
```

This removes all files under the `build/` directory.

## Repository layout

- `bootloader/` — 16-bit MBR loader and 32/64-bit transition stage.
- `kernel/` — 64-bit freestanding kernel sources and linker script.
- `Makefile` — build orchestration that assembles the boot stages, compiles the kernel, and produces a bootable image.

Feel free to hack on the kernel, expand the bootloader, or add new features to MemoriaOS!

## Display mode notes

The bootloader now reports framebuffer geometry through the `BootInfo` structure
and the kernel caches it before handing the dimensions to
`terminal_initialize()`.
The current implementation in `kernel/terminal.c` still targets the classic
80×25 VGA text buffer, so it clamps the reported width/height to that region and
updates only the visible cells.

To experiment with wider or graphical modes, hook your detection logic into the
`detect_terminal_mode()` helper inside `kernel/terminal.c`.  The function already
selects a `TERMINAL_MODE_*` enum and provides a placeholder branch in
`terminal_initialize()` so you can wire up a framebuffer-backed renderer without
reworking the rest of the terminal API.

## Resolving GitHub merge conflicts

If GitHub reports conflicts when you open a pull request, bring your branch up to date locally and resolve them before pushing:

1. Fetch the latest changes and rebase or merge the current `main` branch:
   ```sh
   git fetch origin
   git rebase origin/main   # or: git merge origin/main
   ```
2. Inspect the conflicted files (`git status` will list them) and edit each file to keep the correct content. Look for the `<<<<<<<`, `=======`, and `>>>>>>>` markers that Git inserts and remove them once you have chosen the desired lines.
3. Stage the resolved files and continue the rebase (or create a merge commit):
   ```sh
   git add path/to/file
   git rebase --continue   # or: git commit
   ```
4. Run `make` to ensure the combined changes still build, then force-push the updated branch to GitHub if you rebased (`git push --force-with-lease`).

GitHub will automatically re-run its checks once your branch no longer has conflicts.

> **Tip:** The build now runs `scripts/check-conflicts.sh` automatically before compiling. If `make` fails with a message about
> conflict markers, revisit the files listed in the error, remove the `<<<<<<<`, `=======`, and `>>>>>>>` lines, and then retry
> the build.

### When the build still fails after pulling

Occasionally `git pull` succeeds but leaves behind an unintended mash-up of old and new sources (for example, you might see
compiler errors such as `VGA_MEMORY undeclared`, `invalid storage class for function`, or multiple `kmain` definitions even
though the merge supposedly completed). When that happens, double-check that your working tree actually matches the latest
`main` branch instead of a partially stitched-together file:

1. Inspect your working tree and look for unexpected local edits:
   ```sh
   git status -sb
   ```
   Any files listed here were either modified locally or still conflicted. If you intended to keep none of those changes,
   restore them from the upstream branch:
   ```sh
   git restore --source origin/main -- path/to/file
   ```
2. If many files are affected (or you are unsure which one introduced the corruption), reset everything back to the remote
   branch and rebuild:
   ```sh
   git fetch origin
   git reset --hard origin/main
   git clean -fd   # drops untracked files/directories such as old build artifacts
   make
   ```

These steps return the repository to the exact contents of `origin/main`, ensuring that a clean `make` run reflects the code
under review on GitHub rather than a stale or partially merged snapshot.
