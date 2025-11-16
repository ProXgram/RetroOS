# RetroOS

This is my first repository created to harbor my first hobby OS.

The name of the operating system is not yet chosen upon, therefore it is defaulted to RetroOS.

RetroOS seeks to be a modern x86-64 OS written in C with a retro style to it, restoring the golden era of operating systems
with a nostalgic feel and a focus on simplicity.

This operating system focuses on retro elements of an operating system, as well as the portability of retro games.

## Getting started

The repository now contains a handcrafted bootloader and a very small 64-bit kernel. The bootloader loads the kernel into
memory, switches the CPU into long mode, and jumps into the kernel. Once running, the kernel clears the VGA text console,
prints a welcome message, and waits for a line of keyboard input before echoing it back to the user.

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

The command produces `build/RetroOS.img`, a raw disk image that contains the boot sector, bootloader, and kernel.

### Run in QEMU

```sh
make run
```

The QEMU window will display the boot banner, show "Hello, world!", and prompt for input. Type a message and press Enter to
see it echoed back by the kernel.

### Cleaning

```sh
make clean
```

This removes all files under the `build/` directory.

## Repository layout

- `bootloader/` — 16-bit MBR loader and 32/64-bit transition stage.
- `kernel/` — 64-bit freestanding kernel sources and linker script.
- `Makefile` — build orchestration that assembles the boot stages, compiles the kernel, and produces a bootable image.

Feel free to hack on the kernel, expand the bootloader, or add new features to RetroOS!

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
