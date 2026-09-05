# StarOS Developer Edition

Sistema operacional hobby x86 32-bit com kernel proprio, GUI e rede.

## Compilar

```bash
# Requisitos: i686-elf-gcc, nasm, grub-mkrescue, qemu-system-i386
make        # compila o kernel
make iso    # gera ISO inicializavel
make qemu   # testa em QEMU
```

## Rodar em QEMU

```bash
make qemu
# ou com rede:
qemu-system-i386 -m 256M -kernel build/staros.elf \
  -net nic,model=ne2k_pci -net user -serial stdio
```

## Estrutura

```
kernel/          Kernel principal
arch/x86/        Bootloader, GDT, IDT, ISR, PIC
mm/              Gerenciador de memoria (heap 8MB)
fs/              VFS (ramfs in-memory)
drivers/         Terminal VGA, teclado, mouse, ATA, NE2000, framebuffer
gui/             Sistema grafico VESA 800x600x32
apps/shell/      StarShell
apps/browser/    StarBrowser (HTTP/HTML)
apps/pkg/        StarPKG (gerenciador de pacotes)
apps/deb_installer/ Instalador .deb com inflate DEFLATE
apps/editor/     Editor de texto
apps/monitor/    Monitor do sistema
sys/             Scheduler, syscalls, IPC
include/         Headers publicos
```

## Comandos do shell

```
help, clear, echo, ls, cat, mkdir, rm, write
mem, uname, uptime, reboot, halt
ifconfig, ping, wget
pkg update/install/remove/upgrade/list/search
deb <arquivo.deb>
browser
```

## Paleta de cores

- Fundo desktop:  #04020F
- Janelas:        #1A0A3A
- Paineis:        #2D1B69
- Destaque:       #7C3AED
- Texto:          #EDE9FE
