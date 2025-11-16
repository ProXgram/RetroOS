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
