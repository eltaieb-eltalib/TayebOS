/* ============================================================
   TayebOS Phase 2 - IDT, PIC remap, PIT timer, IRQ keyboard
   - IDT with 256 entries, exceptions 0-31 + IRQs 32-47
   - 8259 PIC remapped to vectors 0x20-0x2F
   - PIT channel 0 at 100 Hz -> timer_ticks / uptime_seconds
   - PS/2 keyboard on IRQ1, feeding a ring buffer (no polling)
   ============================================================ */
#include <stdint.h>
#include "interrupts.h"

/* print/serial live in kernel.c (made non-static there) */
extern void print(const char *s);
extern void serial_putc(char c);
extern void serial_print(const char *s);

/* ---------- port I/O ---------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ---------- IDT ---------- */
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void *isr_stubs[48];
extern void load_idt(struct idt_ptr *ptr);

static void idt_set_gate(uint8_t n, uint32_t handler, uint8_t flags) {
    idt[n].base_lo = (uint16_t)(handler & 0xFFFF);
    idt[n].sel     = 0x08;          /* kernel code segment */
    idt[n].always0 = 0;
    idt[n].flags   = flags;         /* 0x8E = present, ring 0, 32-bit int gate */
    idt[n].base_hi = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void) {
    for (int i = 0; i < 48; i++)
        idt_set_gate((uint8_t)i, (uint32_t)isr_stubs[i], 0x8E);
    /* Vectors 48-255: point at the default IRQ stub so a stray
       interrupt can never jump through a zero entry. */
    for (int i = 48; i < 256; i++)
        idt_set_gate((uint8_t)i, (uint32_t)isr_stubs[33], 0x8E);
    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)&idt;
    load_idt(&idtp);
}

/* ---------- 8259 PIC ---------- */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_remap(void) {
    outb(PIC1_CMD, 0x11);          /* ICW1: init, expect ICW4 */
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20);         /* ICW2: master vectors 0x20-0x27 */
    outb(PIC2_DATA, 0x28);         /* ICW2: slave  vectors 0x28-0x2F */
    outb(PIC1_DATA, 0x04);         /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);         /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01);
    /* GUI phase: unmask timer (IRQ0), keyboard (IRQ1), cascade (IRQ2)
       on the master, and the PS/2 mouse (IRQ12) on the slave. */
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xEF);
}

static void pic_send_eoi(uint32_t irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

/* ---------- PIT timer ---------- */
volatile uint32_t timer_ticks = 0;
volatile uint32_t uptime_seconds = 0;

void pit_init(uint32_t hz) {
    uint32_t divisor = 1193182 / hz;
    outb(0x43, 0x36);              /* channel 0, mode 3 (square wave) */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

/* ---------- uptime clock, top-right corner of the VGA screen ----------
   Writes directly to fixed cells (row 0, cols 72-79) so it never
   moves the main cursor. Format: "T+12345s". */
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
static void vga_clock_update(void) {
    char buf[8];
    uint32_t s = uptime_seconds;
    buf[0] = 'T'; buf[1] = '+';
    /* 5 decimal digits, zero-padded */
    for (int i = 6; i >= 2; i--) { buf[i] = (char)('0' + (s % 10)); s /= 10; }
    buf[7] = 's';
    for (int i = 0; i < 8; i++)
        VGA_MEMORY[72 + i] = ((uint16_t)(0x0B) << 8) | (uint8_t)buf[i];
}

static void print_u32(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) { print("0"); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) { char tmp[2] = { buf[--i], 0 }; print(tmp); }
}

/* Serial-only number print: safe to call from IRQ context, where
   print() would scribble on the VGA cursor mid-line. */
static void serial_print_u32(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

/* ---------- exception handler ---------- */
static const char *exception_names[32] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound range", "Invalid opcode", "No coprocessor",
    "Double fault", "Coprocessor overrun", "Invalid TSS",
    "Segment not present", "Stack fault", "General protection",
    "Page fault", "Reserved", "x87 FPU error", "Alignment check",
    "Machine check", "SIMD exception", "Virtualization",
    "Control protection", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Security exception", "Reserved"
};

void isr_handler(uint32_t int_no, uint32_t err_code) {
    print("\n\n*** EXCEPTION ");
    print_u32(int_no);
    print(": ");
    print(int_no < 32 ? exception_names[int_no] : "Unknown");
    print("  err=");
    print_u32(err_code);
    print(" ***\nSystem halted.\n");
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* ---------- keyboard (IRQ1) ring buffer ---------- */
static const char scancode_ascii[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

#define KB_BUF_SIZE 128
static volatile uint8_t kb_buf[KB_BUF_SIZE];
static volatile uint8_t kb_head = 0;   /* written by IRQ1 */
static volatile uint8_t kb_tail = 0;   /* read by the main loop */

static void kb_enqueue(char c) {
    uint8_t next = (uint8_t)(kb_head + 1);
    if (next == kb_tail) return;       /* buffer full: drop the key */
    kb_buf[kb_head] = (uint8_t)c;
    kb_head = next;
}

int kb_getchar(void) {
    if (kb_head == kb_tail) return -1;
    int c = kb_buf[kb_tail];
    kb_tail = (uint8_t)(kb_tail + 1);
    return c;
}

/* ---------- IRQ dispatcher ---------- */
void irq_handler(uint32_t irq_no) {
    if (irq_no == 0) {                 /* PIT timer, 100 Hz */
        timer_ticks++;
        if (timer_ticks % 100 == 0) {
            uptime_seconds++;
            vga_clock_update();
            serial_print("[timer] uptime ");
            serial_print_u32(uptime_seconds);
            serial_print("s\n");
        }
    } else if (irq_no == 1) {          /* PS/2 keyboard */
        uint8_t sc = inb(0x60);
        if (!(sc & 0x80) && sc < 128) {
            char c = scancode_ascii[sc];
            if (c) kb_enqueue(c);
        }
    } else if (irq_no == 12) {         /* PS/2 mouse: forward raw byte */
        extern void mouse_on_byte(uint8_t data);
        mouse_on_byte(inb(0x60));
    }
    pic_send_eoi(irq_no);
}
