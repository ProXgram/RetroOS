
NASM       ?= nasm
CC         ?= gcc
LD         ?= ld
OBJCOPY    ?= objcopy

CFLAGS := -std=gnu11 -O2 -ffreestanding -fno-stack-protector -fcf-protection=none -fno-pic -mno-red-zone -nostdlib -nostartfiles -Wall -Wextra -Ikernel/include -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 -mno-avx
NASMFLAGS := -Wall -Werror

BUILD_DIR := build
BOOT_BIN := $(BUILD_DIR)/boot.bin
STAGE2_BIN := $(BUILD_DIR)/stage2.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
PAYLOAD_BIN := $(BUILD_DIR)/stage2_kernel.bin
OS_IMAGE := $(BUILD_DIR)/RetroOS.img
KERNEL_SRCS := $(wildcard kernel/*.c)
KERNEL_OBJS := $(patsubst kernel/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SRCS))

.PHONY: all clean run

all: $(OS_IMAGE)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(KERNEL_ELF): kernel/entry.asm $(KERNEL_OBJS) kernel/linker.ld | $(BUILD_DIR)
	$(NASM) -f elf64 kernel/entry.asm -o $(BUILD_DIR)/entry.o
	$(LD) -nostdlib -z max-page-size=0x1000 -T kernel/linker.ld -o $@ $(BUILD_DIR)/entry.o $(KERNEL_OBJS)

$(BUILD_DIR)/%.o: kernel/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(KERNEL_ELF) | $(BUILD_DIR)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@

$(STAGE2_BIN): bootloader/stage2.asm $(KERNEL_BIN) | $(BUILD_DIR)
	@KERNEL_SIZE=$$(stat -c%s $(KERNEL_BIN)); \
	$(NASM) -f bin $(NASMFLAGS) -DKERNEL_SIZE_BYTES=$$KERNEL_SIZE bootloader/stage2.asm -o $@

$(PAYLOAD_BIN): $(STAGE2_BIN) $(KERNEL_BIN) | $(BUILD_DIR)
	cat $(STAGE2_BIN) $(KERNEL_BIN) > $@

$(BOOT_BIN): bootloader/boot.asm $(PAYLOAD_BIN) | $(BUILD_DIR)
	@TOTAL_SIZE=$$(stat -c%s $(PAYLOAD_BIN)); \
	TOTAL_SECTORS=$$(( (TOTAL_SIZE + 511) / 512 )); \
	$(NASM) -f bin $(NASMFLAGS) -DTOTAL_SECTORS=$$TOTAL_SECTORS bootloader/boot.asm -o $@

$(OS_IMAGE): $(BOOT_BIN) $(PAYLOAD_BIN)
	cat $(BOOT_BIN) $(PAYLOAD_BIN) > $@
	@SIZE=$$(stat -c%s $(PAYLOAD_BIN)); \
	SECTORS=$$(( (SIZE + 511) / 512 )); \
	PADDING=$$(( SECTORS * 512 - SIZE )); \
	if [ $$PADDING -gt 0 ]; then \
		dd if=/dev/zero bs=1 count=$$PADDING >> $@ 2>/dev/null; \
	fi

clean:
	rm -rf $(BUILD_DIR)

run: $(OS_IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE)
