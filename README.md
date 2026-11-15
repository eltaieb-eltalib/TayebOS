# TayebOS — v1.2 (Glass GUI + USB boot + AI direction)

نظام تشغيل حقيقي يُقلع من BIOS — بواجهة زجاجية، ويُحرق على فلاشة بـ Rufus.

A real, bootable operating system — glass GUI, flashable to USB with Rufus.

## Burn it to a USB stick with Rufus | الحرق على فلاشة
Use **`tayebos-usb.img`** (16 MB raw image):
1. Plug in a USB stick (it will be erased).
2. Open Rufus, select your stick under *Device*.
3. Click *SELECT* and choose `tayebos-usb.img`.
4. Click *START* — when Rufus asks, choose **DD mode**
   ("Write in DD Image mode"), not ISO mode.
5. Boot the PC from the stick (boot menu key, e.g. F12 / F8 / Esc).

**Requirements on the PC:** Legacy/CSM boot must be enabled in the
BIOS/UEFI setup and Secure Boot disabled. TayebOS is a BIOS
operating system; a UEFI-only machine with no CSM cannot boot it
(a UEFI bootloader is a future phase).

خطوات سريعة: اختر الفلاشة في Rufus، اختر ملف `tayebos-usb.img`،
اضغط START واختر **DD mode**، ثم أقلع من الفلاشة مع تفعيل
Legacy/CSM وتعطيل Secure Boot من إعدادات الـ BIOS.

## What works now | ما يعمل الآن
- **Bootloader v1.1** (`boot/boot.asm`) — EDD LBA kernel load
  (AH=42h) with a CHS one-sector-at-a-time fallback, BIOS font
  copy to `0x9000`, VGA Mode 13h, A20 + flat GDT, protected mode.
  Verified booting in QEMU from floppy, IDE hard disk, **and
  USB storage** (2026-11-15).
- **Interrupts** — IDT (exceptions + IRQs), PIC remapped, PIT
  100 Hz, IRQ1 keyboard ring buffer, IRQ12 PS/2 mouse.
- **Memory** — identity paging (16 MB), `kmalloc` bump heap.
- **Scheduler** — cooperative GUI + background counter tasks.
- **RAM filesystem** — 4 files, `files` command / FILES window.
- **Glass GUI** (`kernel/kernel.c`) — custom DAC palette:
  gradient desktop, translucent-look glass windows with gloss
  sheen, rounded corners and checker shadows, glass taskbar
  with START + uptime clock, terminal (`help`, `about`,
  `files`, `clear`), mouse cursor, clickable icons.

## Build & run | البناء والتشغيل
Requirements: `nasm`, `gcc` (32-bit), `make`, `qemu-system-i386`
```bash
make            # builds tayebos.img (floppy) + tayebos-usb.img (USB)
make run        # floppy in QEMU
make run-usb    # USB image as hard disk in QEMU
```

## Files | الملفات
```
boot/boot.asm           Boot sector: EDD/CHS load, font, Mode 13h
kernel/kernel.c         Glass GUI, paging, heap, mouse, RAM files
kernel/interrupts.c/.h  IDT, PIC, PIT, IRQ keyboard/mouse dispatch
kernel/isr.asm          ISR/IRQ stubs + handler table
kernel/tasks.asm        Cooperative task_switch
kernel/kernel_entry.asm Entry stub
kernel/linker.ld        Kernel at 0x10000, sentinel section last
tayebos.img             Floppy image (QEMU / emulators)
tayebos-usb.img         16 MB image for Rufus (DD mode)
screenshot-v11.png      v1.1 glass desktop, booted from USB image
screenshot-v12.png      v1.2 terminal answering the `ai` command
ai-edition/             AI Edition plan + Ollama scripts (see AI_GOAL.md)
```

## Known notes | ملاحظات
- Kernel load is capped at 64 sectors (32 KiB); the kernel is
  ~16 KiB today. The EDD path reads it in two 32-sector chunks.
- Mouse movement uses standard PS/2 packets on IRQ12; the
  cursor renders and clicks are handled in the GUI loop.
- Next frontiers: VESA 800x600 graphics, a FAT filesystem on
  the USB stick itself, and a UEFI bootloader for CSM-less PCs.

## AI direction (v1.2) | اتجاه الذكاء الاصطناعي
Project goal (set 2026-11-15): an OS that sips RAM/CPU — the
opposite of Windows — runs AI models, adapts to any RAM size,
with a very light browser and a very fast code editor.
- This kernel boots and runs in **4 MB of RAM** (QEMU-verified
  at 4 / 8 / 16 MB). Windows 11 asks for 4 GB minimum.
- Terminal commands `ai` and `mem` carry the plan inside the OS.
- Ollama itself needs a Linux-class kernel, so the product track
  is **TayebOS AI Edition** on Alpine Linux: Ollama on-demand,
  lite-xl editor, NetSurf/Falkon browsers, zram, one-model tuning.
  Full plan, model-by-RAM table and working scripts:
  `ai-edition/AI_GOAL.md`, `ai-edition/select-model.sh`,
  `ai-edition/ai`, `ai-edition/setup.sh`.
- **AI Edition image BUILT and verified (2026-11-15):**
  `ai-edition/tayebos-ai.img` (2 GB, Alpine 3.24 + Ollama 0.17.7,
  qwen2.5:0.5b + llama3.2:1b baked in, auto-picked by RAM).
  Measured in QEMU: **62-65 MB idle RAM**,
  596 MB with the model loaded, and a real Arabic generation
  completed end-to-end. Details: `ai-edition/AI_GOAL.md`.
- **Panel + Start menu (2026-11-15):** glass Waybar taskbar with a
  TayebOS start button, open-window buttons and a clock; a bemenu
  Start menu (AI Chat / Terminal / Editor / Browser / power) plus a
  right-click desktop menu. Real keyboard+mouse input fixed via
  eudev (the session runs under dbus-run-session). Full desktop +
  panel + browser: **~102-113 MB RAM**. See
  `ai-edition/screenshot-panel.png`.
- **Desktop verified too:** typing `desktop` on tty1 launches
  labwc/Wayland with foot + lite-xl autostarted; total RAM with
  the full desktop running: **107 MB**. Screenshot:
  `ai-edition/screenshot-desktop.png`.
