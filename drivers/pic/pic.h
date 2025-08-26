#ifndef PIC_H
#define PIC_H

#include <cldtypes.h>

#define PIC_MASTER_CMD  0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_CMD   0xA0
#define PIC_SLAVE_DATA  0xA1

#define PIC_EOI         0x20

void pic_init(void);
void pic_send_eoi(u8 irq);
void pic_enable_irq(u8 irq);
void pic_disable_irq(u8 irq);

#endif // PIC_H