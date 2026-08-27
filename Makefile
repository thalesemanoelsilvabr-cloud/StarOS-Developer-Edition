# StarOS Beta Edition — Makefile
CC     = i686-elf-gcc
AS     = nasm
LD     = i686-elf-ld
GRUB   = grub-mkrescue

CFLAGS = -m32 -std=c11 -ffreestanding -fno-pic -fno-stack-protector \
         -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
         -Iinclude -O2

ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld --gc-sections

BUILD   = build
ISO_DIR = $(BUILD)/iso
KERNEL  = $(BUILD)/staros.elf
ISO     = $(BUILD)/staros.iso

# Fontes
SRCS_C = \
    kernel/kernel.c kernel/kprintf.c kernel/panic.c \
    arch/x86/gdt.c arch/x86/idt.c arch/x86/pic.c arch/x86/isr.c \
    mm/kmalloc.c \
    fs/vfs.c fs/ramfs/ramfs.c fs/devfs/devfs.c \
    drivers/terminal.c drivers/keyboard.c \
    drivers/input/mouse.c drivers/storage/ata.c \
    drivers/gpu/vga.c \
    drivers/net/ne2000.c drivers/net/net.c drivers/net/http.c \
    sys/scheduler/sched.c sys/syscall/syscall.c \
    sys/ipc/pipe.c sys/init/init.c sys/init/timer.c \
    gui/src/framebuffer.c gui/src/window.c \
    gui/src/widget.c gui/src/gui.c \
    apps/shell/shell.c apps/shell/shell_net_cmds.c \
    apps/editor/editor.c apps/monitor/monitor.c \
    apps/browser/browser.c \
    apps/pkg/pkg.c \
    apps/deb_installer/deb_installer.c

SRCS_ASM = arch/x86/boot.asm arch/x86/stubs.asm

OBJS_C   = $(patsubst %.c,  $(BUILD)/%.o, $(SRCS_C))
OBJS_ASM = $(patsubst %.asm,$(BUILD)/%.o, $(SRCS_ASM))
OBJS     = $(OBJS_ASM) $(OBJS_C)

.PHONY: all iso qemu qemu-net clean help

all: $(KERNEL)

$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(BUILD)
	@echo "[LD] $@"
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "==> Kernel: $@"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) $< -o $@

iso: $(KERNEL)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL) $(ISO_DIR)/boot/staros.elf
	@cp etc/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB) -o $(ISO) $(ISO_DIR)
	@echo "==> ISO: $(ISO)"

qemu: $(KERNEL)
	qemu-system-i386 -m 256M -kernel $(KERNEL) \
	  -net nic,model=ne2k_pci -net user \
	  -serial stdio -rtc base=localtime

qemu-net: $(ISO)
	qemu-system-i386 -m 256M -cdrom $(ISO) -boot d \
	  -net nic,model=ne2k_pci -net user \
	  -net user,hostfwd=tcp::8080-:80 \
	  -serial stdio

clean:
	rm -rf $(BUILD)
	@echo "Limpo."

help:
	@echo "make        — compila o kernel"
	@echo "make iso    — gera ISO"
	@echo "make qemu   — testa em QEMU"
	@echo "make clean  — limpa build"
