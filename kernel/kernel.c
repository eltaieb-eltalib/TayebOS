/* ============================================================
   TayebOS Kernel v1.0 — GUI phase
   - Mode 13h graphics (320x200x256), framebuffer 0xA0000
   - BIOS 8x8 font copied by the bootloader to 0x9000
   - Interrupts from interrupts.c: IDT + PIC + PIT 100 Hz,
     IRQ1 keyboard ring buffer, IRQ12 PS/2 mouse bytes
   - Identity paging (16 MB), bump heap (kmalloc)
   - Cooperative scheduler: GUI task + background task
   - Tiny RAM filesystem, windowed desktop GUI
   ============================================================ */
#include <stdint.h>
#include <stddef.h>
#include "interrupts.h"

/* ---------- small libc ---------- */
void *memset(void *dst, int v, size_t n) { uint8_t *d = dst; while (n--) *d++ = (uint8_t)v; return dst; }
void *memcpy(void *dst, const void *src, size_t n) { uint8_t *d = dst; const uint8_t *s = src; while (n--) *d++ = *s++; return dst; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (uint8_t)*a - (uint8_t)*b; }
static void strcpy_(char *d, const char *s) { while ((*d++ = *s++)) { } }

/* ---------- port I/O ---------- */
static inline void outb(uint16_t port, uint8_t val) { __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port)); return r; }

/* ---------- serial COM1 log (non-static: interrupts.c uses them) ---------- */
#define SERIAL_PORT 0x3F8
static void serial_init(void) {
    outb(SERIAL_PORT + 1, 0x00); outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03); outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03); outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
}
void serial_putc(char c) { while ((inb(SERIAL_PORT + 5) & 0x20) == 0) { } outb(SERIAL_PORT, (uint8_t)c); }
void serial_print(const char *s) { while (*s) serial_putc(*s++); }
/* interrupts.c's exception handler prints through print(): in graphics
   mode there is no text screen, so route it to the serial log. */
void print(const char *s) { serial_print(s); }

/* ---------- graphics: Mode 13h ---------- */
#define FB ((uint8_t *)0xA0000)
#define SCR_W 320
#define SCR_H 200
#define FONT ((const uint8_t *)0x9000)
enum { BLACK=0, BLUE=1, GREEN=2, CYAN=3, RED=4, MAGENTA=5, BROWN=6, LGRAY=7,
       DGRAY=8, LBLUE=9, LGREEN=10, LCYAN=11, LRED=12, LMAGENTA=13, YELLOW=14, WHITE=15 };

static void putpixel(int x, int y, uint8_t c) {
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) FB[y * SCR_W + x] = c;
}
static void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) putpixel(i, j, c);
}
static void draw_border(int x, int y, int w, int h, uint8_t c) {
    fill_rect(x, y, w, 1, c); fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c); fill_rect(x + w - 1, y, 1, h, c);
}
static void draw_char(int x, int y, char ch, uint8_t color) {
    const uint8_t *glyph = FONT + (uint8_t)ch * 8;
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++)
            if (bits & (0x80 >> col)) putpixel(x + col, y + row, color);
    }
}
static void draw_string(int x, int y, const char *s, uint8_t color) {
    while (*s) { draw_char(x, y, *s, color); x += 8; s++; }
}
static void draw_number(int x, int y, uint32_t v, uint8_t color) {
    char buf[12]; int i = 0;
    if (v == 0) { draw_char(x, y, '0', color); return; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i) { draw_char(x, y, buf[--i], color); x += 8; }
}
static void format_time(char *out, uint32_t seconds) {
    uint32_t hh = (seconds / 3600) % 100, mm = (seconds / 60) % 60, ss = seconds % 60;
    out[0] = '0' + hh / 10; out[1] = '0' + hh % 10; out[2] = ':';
    out[3] = '0' + mm / 10; out[4] = '0' + mm % 10; out[5] = ':';
    out[6] = '0' + ss / 10; out[7] = '0' + ss % 10; out[8] = 0;
}

/* ---------- GUI state ---------- */
static volatile int gui_dirty = 1;
static char input_buf[48];
static int input_len = 0;
static char response_line[40] = "TYPE HELP AND PRESS ENTER";
static volatile int files_open = 0;
static volatile int about_open = 0;

/* ---------- terminal commands (run from the GUI loop) ---------- */
static void run_command(void) {
    input_buf[input_len] = 0;
    if (strcmp(input_buf, "help") == 0)       strcpy_(response_line, "CMDS: HELP AI MEM FILES CLEAR");
    else if (strcmp(input_buf, "about") == 0) strcpy_(response_line, "TAYEBOS V1.2 - BY ELTAYEB");
    else if (strcmp(input_buf, "files") == 0) { files_open = 1; strcpy_(response_line, "RAMDISK OPEN: 4 FILES"); }
    else if (strcmp(input_buf, "ai") == 0)    strcpy_(response_line, "AI EDITION: OLLAMA BY RAM SIZE");
    else if (strcmp(input_buf, "mem") == 0)   strcpy_(response_line, "BOOTS IN 4 MB - PAGING 16 MB");
    else if (strcmp(input_buf, "clear") == 0) strcpy_(response_line, "");
    else if (input_len > 0)                   strcpy_(response_line, "UNKNOWN CMD - TYPE HELP");
    input_len = 0; input_buf[0] = 0;
}
static void keyboard_poll(void) {
    int c;
    while ((c = kb_getchar()) >= 0) {
        if (c == '\n') run_command();
        else if (c == '\b') { if (input_len > 0) input_buf[--input_len] = 0; }
        else if (c >= ' ' && input_len < 40) { input_buf[input_len++] = (char)c; input_buf[input_len] = 0; }
        gui_dirty = 1;
    }
}

/* ---------- PS/2 mouse: bytes arrive via IRQ12 (interrupts.c) ---------- */
static volatile int mouse_x = 160, mouse_y = 100;
static volatile int mouse_moved = 0, mouse_clicked = 0, click_x = 0, click_y = 0;
static uint8_t mouse_cycle = 0, mouse_bytes[3];
void mouse_on_byte(uint8_t data) {
    if (mouse_cycle == 0 && !(data & 0x08)) return; /* wait for sync bit */
    mouse_bytes[mouse_cycle++] = data;
    if (mouse_cycle < 3) return;
    mouse_cycle = 0;
    int dx = mouse_bytes[1] - ((mouse_bytes[0] << 4) & 0x100);
    int dy = mouse_bytes[2] - ((mouse_bytes[0] << 3) & 0x100);
    mouse_x += dx; mouse_y -= dy;
    if (mouse_x < 0) mouse_x = 0; if (mouse_x > SCR_W - 2) mouse_x = SCR_W - 2;
    if (mouse_y < 0) mouse_y = 0; if (mouse_y > SCR_H - 2) mouse_y = SCR_H - 2;
    mouse_moved = 1;
    if (mouse_bytes[0] & 0x01) { mouse_clicked = 1; click_x = mouse_x; click_y = mouse_y; }
}
static void ps2_wait_write(void) { int t = 100000; while ((inb(0x64) & 0x02) && t--) { } }
static void ps2_wait_read(void)  { int t = 100000; while (!(inb(0x64) & 0x01) && t--) { } }
static void mouse_write(uint8_t cmd) {
    ps2_wait_write(); outb(0x64, 0xD4);
    ps2_wait_write(); outb(0x60, cmd);
    ps2_wait_read();  (void)inb(0x60); /* ACK */
}
static void mouse_init(void) {
    ps2_wait_write(); outb(0x64, 0xA8);            /* enable auxiliary device */
    ps2_wait_write(); outb(0x64, 0x20);            /* read config */
    ps2_wait_read();  uint8_t cfg = inb(0x60);
    cfg |= 0x02; cfg &= ~0x20;                     /* IRQ12 on, mouse clock on */
    ps2_wait_write(); outb(0x64, 0x60);            /* write config */
    ps2_wait_write(); outb(0x60, cfg);
    mouse_write(0xF6);                             /* set defaults */
    mouse_write(0xF4);                             /* enable data reporting */
}

/* ---------- paging: identity-map first 16 MB ---------- */
static void paging_init(void) {
    uint32_t *pd = (uint32_t *)0x100000;
    uint32_t *pt = (uint32_t *)0x101000;
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 1024; i++)
            pt[t * 1024 + i] = ((uint32_t)(t * 1024 + i) * 0x1000) | 3;
        pd[t] = (0x101000 + (uint32_t)t * 0x1000) | 3;
    }
    for (int t = 4; t < 1024; t++) pd[t] = 0;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pd));
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000; /* PG + WP */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
}

/* ---------- heap: simple bump allocator ---------- */
static uint32_t heap_next = 0x200000;
static void *kmalloc(size_t size) {
    heap_next = (heap_next + 15) & ~15u;
    void *p = (void *)heap_next;
    heap_next += size;
    memset(p, 0, size);
    return p;
}

/* ---------- RAM filesystem ---------- */
struct ramfile { const char *name; uint32_t size; };
static const struct ramfile ram_files[] = {
    { "README.TXT", 128 }, { "KERNEL.C", 18432 },
    { "BOOT.ASM", 4096 },   { "NOTES.TXT", 256 },
};
#define RAM_FILE_COUNT 4

/* ---------- cooperative scheduler: GUI + background task ---------- */
extern void task_switch(uint32_t *old_esp_save, uint32_t new_esp);
static uint32_t gui_esp = 0, bg_esp = 0;
static volatile uint32_t bg_counter = 0;
static void bg_task_entry(void) {
    for (;;) {
        bg_counter++;
        task_switch(&bg_esp, gui_esp);
    }
}
static void scheduler_init(void) {
    uint8_t *stack = kmalloc(4096);
    uint32_t *sp = (uint32_t *)(stack + 4096);
    *--sp = (uint32_t)bg_task_entry; /* return address */
    *--sp = 0; /* esi */
    *--sp = 0; /* edi */
    *--sp = 0; /* ebx */
    *--sp = 0; /* ebp */
    bg_esp = (uint32_t)sp;
}

/* ---------- glass theme: custom DAC palette + gradients ---------- */
#define PAL_BG_BASE 16     /* 32 entries: desktop gradient */
#define PAL_TITLE_BASE 48  /* 16 entries: title/taskbar gradient */
#define PAL_GLASS_BASE 96  /* 16 entries: light glass body gradient */
#define PAL_GREEN_BASE 112 /* 8 entries: START button gradient */
#define COL_SHADOW 66
#define COL_GLOSS 67
#define COL_DARKTEXT 68
#define COL_TERMBG 69
#define COL_SHEEN 71

static void set_dac(int idx, int r, int g, int b) {
    outb(0x3C8, (uint8_t)idx);
    outb(0x3C9, (uint8_t)r); outb(0x3C9, (uint8_t)g); outb(0x3C9, (uint8_t)b);
}
static void palette_ramp(int base, int count, int r0, int g0, int b0, int r1, int g1, int b1) {
    for (int i = 0; i < count; i++)
        set_dac(base + i, r0 + (r1 - r0) * i / (count - 1),
                g0 + (g1 - g0) * i / (count - 1), b0 + (b1 - b0) * i / (count - 1));
}
static void palette_init(void) {
    palette_ramp(PAL_BG_BASE, 32,    3, 7, 26,   5, 30, 34);   /* navy -> teal */
    palette_ramp(PAL_TITLE_BASE, 16, 5, 12, 36,  18, 36, 60);  /* deep blue -> sky */
    palette_ramp(PAL_GLASS_BASE, 16, 57, 61, 63, 39, 48, 58);  /* white-blue -> steel */
    palette_ramp(PAL_GREEN_BASE, 8,  7, 28, 9,   20, 52, 22);  /* START green */
    set_dac(COL_SHADOW, 2, 4, 15);
    set_dac(COL_GLOSS, 63, 63, 63);
    set_dac(COL_DARKTEXT, 3, 6, 18);
    set_dac(COL_TERMBG, 2, 4, 11);
    set_dac(COL_SHEEN, 10, 16, 30);
}
static int corner_inset(int j, int h) {
    static const int ci[4] = { 3, 2, 1, 0 };
    int d = j < h - 1 - j ? j : h - 1 - j;
    return d < 4 ? ci[d] : 0;
}
static void fill_rounded_grad(int x, int y, int w, int h, int base, int count) {
    for (int j = 0; j < h; j++) {
        int ins = corner_inset(j, h);
        int idx = base + (j * count) / h; if (idx >= base + count) idx = base + count - 1;
        fill_rect(x + ins, y + j, w - 2 * ins, 1, (uint8_t)idx);
    }
}
static void fill_rounded_solid(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++) {
        int ins = corner_inset(j, h);
        fill_rect(x + ins, y + j, w - 2 * ins, 1, c);
    }
}
static void gloss_overlay(int x, int y, int w, int rows, uint8_t color) {
    for (int j = 0; j < rows; j++)
        for (int i = 2; i < w - 2; i++)
            if (((i + j) & 1) == 0) putpixel(x + i, y + j, color);
}
static void shadow_rounded(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        int ins = corner_inset(j, h);
        for (int i = ins; i < w - ins; i++)
            if (((i + j) & 1) == 0) putpixel(x + i, y + j, COL_SHADOW);
    }
}
static void rounded_border(int x, int y, int w, int h, uint8_t c) {
    fill_rect(x + 3, y, w - 6, 1, c); fill_rect(x + 3, y + h - 1, w - 6, 1, c);
    fill_rect(x, y + 3, 1, h - 6, c); fill_rect(x + w - 1, y + 3, 1, h - 6, c);
    putpixel(x + 2, y + 1, c); putpixel(x + w - 3, y + 1, c);
    putpixel(x + 1, y + 2, c); putpixel(x + w - 2, y + 2, c);
    putpixel(x + 2, y + h - 2, c); putpixel(x + w - 3, y + h - 2, c);
    putpixel(x + 1, y + h - 3, c); putpixel(x + w - 2, y + h - 3, c);
}
static void draw_string_shadow(int x, int y, const char *s, uint8_t fg) {
    draw_string(x + 1, y + 1, s, COL_DARKTEXT);
    draw_string(x, y, s, fg);
}

/* ---------- GUI drawing (glass) ---------- */
static const char *cursor_sprite[11] = {
    "X.......", "XX......", "XWX.....", "XWWX....", "XWWWX...",
    "XWWWWX..", "XWWWWWX.", "XWWWX...", "XWXXWX..", "XX.XWX..", "....XX.."
};
static void draw_cursor(int x, int y) {
    for (int r = 0; r < 11; r++)
        for (int c = 0; c < 8; c++) {
            char p = cursor_sprite[r][c];
            if (p == 'X') putpixel(x + c, y + r, BLACK);
            else if (p == 'W') putpixel(x + c, y + r, WHITE);
        }
}
static void draw_window(int x, int y, int w, int h, const char *title, uint8_t unused) {
    (void)unused;
    shadow_rounded(x + 5, y + 6, w, h);
    fill_rounded_grad(x, y, w, h, PAL_GLASS_BASE, 16);
    for (int j = 1; j < 14; j++) {           /* glass title bar */
        int ins = corner_inset(j, h);
        int idx = PAL_TITLE_BASE + (j * 16) / 14;
        fill_rect(x + ins, y + j, w - 2 * ins, 1, (uint8_t)idx);
    }
    gloss_overlay(x, y + 1, w, 6, COL_GLOSS);        /* title shine */
    gloss_overlay(x, y + 14, w, h / 3, COL_GLOSS);   /* body sheen */
    rounded_border(x, y, w, h, COL_GLOSS);
    draw_string_shadow(x + 6, y + 3, title, WHITE);
}
static void icon_tile(int x, int y, const char *label) {
    shadow_rounded(x + 3, y + 3, 28, 22);
    fill_rounded_grad(x, y, 28, 22, PAL_TITLE_BASE, 16);
    gloss_overlay(x, y + 1, 28, 8, COL_GLOSS);
    rounded_border(x, y, 28, 22, COL_GLOSS);
    draw_string_shadow(x + 14 - (int)strlen(label) * 4, y + 25, label, WHITE);
}
static void gui_draw(void) {
    /* desktop: vertical gradient */
    for (int j = 0; j < SCR_H; j++) {
        int idx = PAL_BG_BASE + (j * 32) / SCR_H;
        fill_rect(0, j, SCR_W, 1, (uint8_t)idx);
    }
    /* glass icon tiles */
    icon_tile(12, 10, "FILES");
    icon_tile(12, 50, "TERM");
    icon_tile(12, 90, "INFO");

    /* welcome window (glass) */
    draw_window(64, 12, 242, 106, "WELCOME TO TAYEBOS", BLUE);
    int tx = 74, ty = 32;
    draw_string(tx, ty,      "TAYEBOS V1.2 - GLASS GUI", COL_DARKTEXT);
    draw_string(tx, ty + 11, "KERNEL : 32-BIT PROTECTED", COL_DARKTEXT);
    draw_string(tx, ty + 22, "INTS   : IDT PIT KB MOUSE", COL_DARKTEXT);
    draw_string(tx, ty + 33, "PAGING : 16 MB IDENTITY", COL_DARKTEXT);
    draw_string(tx, ty + 44, "HEAP   : KMALLOC ACTIVE", COL_DARKTEXT);
    draw_string(tx, ty + 55, "TASKS  : GUI + BACKGROUND", COL_DARKTEXT);
    draw_string(tx, ty + 66, "BG COUNT:", COL_DARKTEXT);
    draw_number(tx + 80, ty + 66, bg_counter, RED);

    /* terminal window (dark glass) */
    shadow_rounded(69, 132, 242, 50);
    fill_rounded_solid(64, 126, 242, 50, COL_TERMBG);
    for (int j = 1; j < 12; j++) {
        int ins = corner_inset(j, 50);
        int idx = PAL_TITLE_BASE + (j * 16) / 12;
        fill_rect(64 + ins, 126 + j, 242 - 2 * ins, 1, (uint8_t)idx);
    }
    gloss_overlay(64, 127, 242, 5, COL_GLOSS);
    gloss_overlay(64, 138, 242, 12, COL_SHEEN);
    rounded_border(64, 126, 242, 50, COL_GLOSS);
    draw_string_shadow(70, 129, "TERMINAL", WHITE);
    draw_string(70, 144, response_line, LGREEN);
    draw_string(70, 158, ">", LGREEN);
    draw_string(78, 158, input_buf, LGREEN);
    if ((timer_ticks / 50) % 2 == 0) fill_rect(78 + (int)strlen(input_buf) * 8, 158, 7, 8, LGREEN);

    /* files window (RAM disk, glass) */
    if (files_open) {
        draw_window(96, 30, 184, 100, "RAMDISK FILES", MAGENTA);
        for (int i = 0; i < RAM_FILE_COUNT; i++) {
            draw_string(106, 50 + i * 12, ram_files[i].name, COL_DARKTEXT);
            draw_number(226, 50 + i * 12, ram_files[i].size, BLUE);
        }
        draw_string(106, 104, "4 FILES IN RAM", COL_DARKTEXT);
    }
    /* about window (glass) */
    if (about_open) {
        draw_window(104, 52, 176, 72, "ABOUT", GREEN);
        draw_string(114, 72, "TAYEBOS V1.2", COL_DARKTEXT);
        draw_string(114, 84, "BUILT FOR ELTAYEB", COL_DARKTEXT);
        draw_string(114, 96, "GUI: GLASS EDITION", COL_DARKTEXT);
    }

    /* taskbar (glass) */
    for (int j = 0; j < 18; j++) {
        int idx = PAL_TITLE_BASE + (j * 16) / 18;
        fill_rect(0, 182 + j, SCR_W, 1, (uint8_t)idx);
    }
    fill_rect(0, 182, SCR_W, 1, COL_GLOSS);
    fill_rounded_grad(4, 185, 46, 12, PAL_GREEN_BASE, 8);
    gloss_overlay(4, 186, 46, 5, COL_GLOSS);
    rounded_border(4, 185, 46, 12, COL_GLOSS);
    draw_string_shadow(11, 187, "START", WHITE);
    draw_string_shadow(64, 187, "TAYEBOS DESKTOP", WHITE);
    char tbuf[12]; format_time(tbuf, timer_ticks / 100);
    draw_string_shadow(248, 187, tbuf, WHITE);

    draw_cursor(mouse_x, mouse_y);
}

static void handle_click(int x, int y) {
    if (x >= 12 && x <= 38 && y >= 10 && y <= 30) files_open = !files_open;
    else if (x >= 12 && x <= 38 && y >= 90 && y <= 110) about_open = !about_open;
    else if (x >= 4 && x <= 50 && y >= 185 && y <= 197) about_open = !about_open;
    gui_dirty = 1;
}

/* sentinel in the very last section of the kernel binary: if the
   bootloader load were truncated, this string would be garbled */
__attribute__((section(".sentinel"), used))
const char kernel_sentinel[] = "TAYEBOS-END-OF-KERNEL-OK";

/* ---------- kernel main ---------- */
void kernel_main(void) {
    serial_init();
    serial_print("\n[TayebOS v1.2] kernel_main: graphics mode\n");

    idt_init();
    pic_remap();
    pit_init(100);
    serial_print("[OK] IDT + PIC remapped + PIT at 100 Hz (phase 2)\n");

    paging_init();
    serial_print("[OK] Paging enabled: identity map 16 MB (phase 3)\n");

    char *heap_test = kmalloc(32);
    strcpy_(heap_test, "HEAP-OK");
    serial_print("[OK] Heap kmalloc: "); serial_print(heap_test); serial_putc('\n');

    scheduler_init();
    serial_print("[OK] Scheduler: 2 tasks, GUI + background (phase 4)\n");

    mouse_init();
    serial_print("[OK] PS/2 mouse initialized (IRQ12)\n");

    serial_print("[SENTINEL] "); serial_print(kernel_sentinel); serial_putc('\n');

    palette_init();
    serial_print("[OK] Glass theme palette (DAC) programmed\n");

    interrupts_enable();
    serial_print("[OK] Interrupts on - entering GUI loop (phase 6)\n");

    uint32_t last_second = 0xFFFFFFFF;
    gui_draw();
    for (;;) {
        task_switch(&gui_esp, bg_esp);          /* run background task briefly */
        keyboard_poll();
        if (mouse_clicked) { mouse_clicked = 0; handle_click(click_x, click_y); }
        uint32_t sec = timer_ticks / 100;
        if (sec != last_second || gui_dirty || mouse_moved) {
            last_second = sec; gui_dirty = 0; mouse_moved = 0;
            gui_draw();
        }
        __asm__ volatile ("hlt");               /* wake on next IRQ */
    }
}
