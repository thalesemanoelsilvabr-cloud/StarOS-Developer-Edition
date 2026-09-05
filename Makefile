# StarOS Beta Edition — Makefile
# Usa o cross compiler i686-elf quando disponivel; senao cai para o gcc
# do host em modo 32-bit (precisa de gcc-multilib).
CROSS   := $(shell command -v i686-elf-gcc 2>/dev/null)
ifeq ($(CROSS),)
CC      := gcc
LD      := ld
else
CC      := i686-elf-gcc
LD      := i686-elf-ld
endif
AS      := nasm
GRUB    := grub-mkrescue

CFLAGS  := -m32 -std=c11 -ffreestanding \
            -fno-pic -fno-stack-protector \
            -fno-builtin -nostdinc \
            -Wall -Wextra \
            -Wno-unused-parameter \
            -Wno-unused-function \
            -Iinclude \
            -O2

ASFLAGS := -f elf32
LDFLAGS := -m elf_i386 -T linker.ld --gc-sections -nostdlib

BUILD   := build
ISO_DIR := $(BUILD)/iso
KERNEL  := $(BUILD)/staros.elf
ISO     := $(BUILD)/staros.iso

SRCS_ASM := arch/x86/boot.asm arch/x86/stubs.asm

SRCS_C := \
    kernel/kernel.c kernel/kprintf.c kernel/kstring.c kernel/panic.c \
    arch/x86/gdt.c arch/x86/idt.c arch/x86/pic.c arch/x86/isr.c \
    mm/kmalloc.c \
    fs/vfs.c fs/ramfs/ramfs.c fs/devfs/devfs.c \
    drivers/terminal.c \
    drivers/keyboard/keyboard.c \
    drivers/mouse/mouse.c \
    drivers/storage/ata.c \
    drivers/gpu/vga.c \
    drivers/acpi/acpi.c \
    drivers/wifi/wifi.c \
    drivers/net/ne2000.c \
    drivers/net/net.c \
    drivers/net/http.c \
    sys/scheduler/sched.c sys/syscall/syscall.c \
    sys/ipc/pipe.c sys/init/init.c sys/init/timer.c \
    gui/src/framebuffer.c gui/src/window.c \
    gui/src/widget.c gui/src/gui.c \
    apps/shell/shell.c apps/shell/shell_net_cmds.c \
    apps/editor/editor.c apps/monitor/monitor.c \
    apps/browser/browser.c \
    apps/pkg/pkg.c \
    apps/deb_installer/deb_installer.c \
    apps/taskbar/taskbar.c \
    apps/installer/installer.c \
    apps/settings/settings.c

OBJS_C   := $(patsubst %.c,   $(BUILD)/%.o, $(SRCS_C))
OBJS_ASM := $(patsubst %.asm, $(BUILD)/%.o, $(SRCS_ASM))
OBJS     := $(OBJS_ASM) $(OBJS_C)

.PHONY: all iso qemu qemu-net clean help

all: $(KERNEL)
	@echo "\n  OK StarOS Beta Edition compilado!"
	@echo "  -> $(KERNEL)"
	@echo "  -> make iso && make qemu para testar\n"

$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(BUILD)
	@echo "[LD] $@"
	$(LD) $(LDFLAGS) -o $@ \
	    $(BUILD)/arch/x86/boot.o \
	    $(BUILD)/arch/x86/stubs.o \
	    $(filter-out $(BUILD)/arch/x86/boot.o $(BUILD)/arch/x86/stubs.o,$(OBJS))

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
	$(GRUB) -o $(ISO) $(ISO_DIR) 2>/dev/null
	@echo "  OK ISO: $(ISO)"

qemu: iso
	qemu-system-i386 \
	    -m 256M \
	    -cdrom $(ISO) \
	    -boot d \
	    -net nic,model=ne2k_pci \
	    -net user,hostfwd=tcp::8080-:80 \
	    -serial stdio \
	    -vga std \
	    -rtc base=localtime \
	    -no-reboot

qemu-net: iso
	qemu-system-i386 \
	    -m 256M \
	    -cdrom $(ISO) \
	    -boot d \
	    -net nic,model=ne2k_pci \
	    -net user,hostfwd=tcp::8080-:80,hostfwd=udp::5353-:53 \
	    -serial stdio \
	    -vga std \
	    -no-reboot

clean:
	rm -rf $(BUILD)
	@echo "  OK Limpo."

help:
	@echo "make          compila"
	@echo "make iso      gera ISO"
	@echo "make qemu     testa em QEMU"
	@echo "make clean    limpa"
