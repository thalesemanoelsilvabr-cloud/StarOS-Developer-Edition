# =============================================================
#  StarOS Beta Edition — Makefile
#  Compilador: i686-elf-gcc + nasm
#  Alvo: x86 32-bit bare-metal
# =============================================================

CC      := i686-elf-gcc
AS      := nasm
LD      := i686-elf-ld
GRUB    := grub-mkrescue

# ── Flags ────────────────────────────────────────────────────
CFLAGS  := -m32 -std=c11 -ffreestanding \
            -fno-pic -fno-stack-protector \
            -fno-builtin -nostdinc \
            -Wall -Wextra \
            -Wno-unused-parameter \
            -Wno-unused-function \
            -Iinclude \
            -O2

ASFLAGS := -f elf32

LDFLAGS := -m elf_i386 \
            -T linker.ld \
            --gc-sections \
            -nostdlib

# ── Saídas ───────────────────────────────────────────────────
BUILD   := build
ISO_DIR := $(BUILD)/iso
KERNEL  := $(BUILD)/staros.elf
ISO     := $(BUILD)/staros.iso

# ── Fontes C por subsistema ───────────────────────────────────
SRCS_KERNEL := \
    kernel/kernel.c     \
    kernel/kprintf.c    \
    kernel/kstring.c    \
    kernel/panic.c

SRCS_ARCH := \
    arch/x86/gdt.c      \
    arch/x86/idt.c      \
    arch/x86/pic.c      \
    arch/x86/isr.c

SRCS_MM := \
    mm/kmalloc.c

SRCS_FS := \
    fs/vfs.c            \
    fs/ramfs/ramfs.c    \
    fs/devfs/devfs.c

SRCS_DRIVERS := \
    drivers/terminal.c          \
    drivers/keyboard.c          \
    drivers/input/mouse.c       \
    drivers/storage/ata.c       \
    drivers/gpu/vga.c           \
    drivers/net/ne2000.c        \
    drivers/net/net.c           \
    drivers/net/http.c

SRCS_SYS := \
    sys/scheduler/sched.c   \
    sys/syscall/syscall.c   \
    sys/ipc/pipe.c          \
    sys/init/init.c         \
    sys/init/timer.c

SRCS_GUI := \
    gui/src/framebuffer.c   \
    gui/src/window.c        \
    gui/src/widget.c        \
    gui/src/gui.c

SRCS_APPS := \
    apps/shell/shell.c              \
    apps/shell/shell_net_cmds.c     \
    apps/editor/editor.c            \
    apps/monitor/monitor.c          \
    apps/browser/browser.c          \
    apps/pkg/pkg.c                  \
    apps/deb_installer/deb_installer.c

# ── Fontes ASM ────────────────────────────────────────────────
SRCS_ASM := \
    arch/x86/boot.asm   \
    arch/x86/stubs.asm

# ── Todos os fontes ───────────────────────────────────────────
SRCS_C := \
    $(SRCS_KERNEL)  \
    $(SRCS_ARCH)    \
    $(SRCS_MM)      \
    $(SRCS_FS)      \
    $(SRCS_DRIVERS) \
    $(SRCS_SYS)     \
    $(SRCS_GUI)     \
    $(SRCS_APPS)

# ── Objetos ───────────────────────────────────────────────────
OBJS_C   := $(patsubst %.c,   $(BUILD)/%.o, $(SRCS_C))
OBJS_ASM := $(patsubst %.asm, $(BUILD)/%.o, $(SRCS_ASM))
OBJS     := $(OBJS_ASM) $(OBJS_C)

# ── Regras principais ─────────────────────────────────────────
.PHONY: all iso qemu qemu-net clean help

all: $(KERNEL)
	@echo ""
	@echo "  ✓ StarOS Beta Edition compilado com sucesso!"
	@echo "  → $(KERNEL)"
	@echo "  → Execute: make qemu"
	@echo ""

# ── Link final ────────────────────────────────────────────────
$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(BUILD)
	@echo "[LD] $@"
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/arch/x86/boot.o $(BUILD)/arch/x86/stubs.o $(filter-out $(BUILD)/arch/x86/boot.o $(BUILD)/arch/x86/stubs.o,$(OBJS))

# ── Compilação C ──────────────────────────────────────────────
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ── Compilação ASM ────────────────────────────────────────────
$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) $< -o $@

# ── ISO inicializável ─────────────────────────────────────────
iso: $(KERNEL)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL) $(ISO_DIR)/boot/staros.elf
	@cp etc/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB) -o $(ISO) $(ISO_DIR) 2>/dev/null
	@echo "  ✓ ISO: $(ISO)"

# ── QEMU via ISO (método correto com GRUB+Multiboot2) ────────
qemu: iso
	qemu-system-i386 \
	    -m 256M \
	    -cdrom $(ISO) \
	    -boot d \
	    -net nic,model=ne2k_pci \
	    -net user \
	    -serial stdio \
	    -vga std \
	    -rtc base=localtime \
	    -no-reboot

# ── QEMU modo texto sem ISO ───────────────────────────────────
# Usa GRUB2 instalado localmente para carregar direto
qemu-direct: $(KERNEL)
	qemu-system-i386 \
	    -m 256M \
	    -kernel $(KERNEL) \
	    -append "" \
	    -initrd /dev/null \
	    -net nic,model=ne2k_pci \
	    -net user \
	    -serial stdio \
	    -no-reboot 2>/dev/null || \
	$(MAKE) qemu

# ── QEMU via ISO com rede ─────────────────────────────────────
qemu-net: $(ISO)
	qemu-system-i386 \
	    -m 256M \
	    -cdrom $(ISO) \
	    -boot d \
	    -net nic,model=ne2k_pci \
	    -net user,hostfwd=tcp::8080-:80 \
	    -serial stdio \
	    -no-reboot

# ── Limpeza ───────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)
	@echo "  ✓ Build limpo."

# ── Ajuda ─────────────────────────────────────────────────────
help:
	@echo ""
	@echo "  StarOS Beta Edition — Build System"
	@echo ""
	@echo "  make            Compila o kernel"
	@echo "  make iso        Gera imagem ISO inicializável"
	@echo "  make qemu       Testa em QEMU (kernel direto)"
	@echo "  make qemu-net   Testa ISO em QEMU com rede"
	@echo "  make clean      Remove arquivos de build"
	@echo ""
	@echo "  Requisitos:"
	@echo "    i686-elf-gcc  nasm  i686-elf-ld"
	@echo "    grub-mkrescue (para iso)"
	@echo "    qemu-system-i386 (para qemu)"
	@echo ""