/* KESSEL: the firmware's delay.h (cycle-counter waits) is not needed by the sound engine */
#include <stdint.h>
static inline void DWT_Delay_us(volatile uint32_t us) { (void) us; }
