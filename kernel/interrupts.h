/* TayebOS Phase 2 - interrupts: IDT, PIC, PIT timer, IRQ keyboard */
#ifndef TAYEBOS_INTERRUPTS_H
#define TAYEBOS_INTERRUPTS_H

#include <stdint.h>

void idt_init(void);
void pic_remap(void);
void pit_init(uint32_t hz);
void interrupts_enable(void);

/* C handlers called from the asm stubs in isr.asm */
void isr_handler(uint32_t int_no, uint32_t err_code);
void irq_handler(uint32_t irq_no);

/* Interrupt-driven keyboard: -1 when the buffer is empty */
int kb_getchar(void);

/* Timer state, updated by IRQ0 */
extern volatile uint32_t timer_ticks;
extern volatile uint32_t uptime_seconds;

#endif
