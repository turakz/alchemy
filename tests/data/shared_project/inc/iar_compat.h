#ifndef IAR_COMPAT_H
#define IAR_COMPAT_H

struct IrqConfig {
  int irqNumber;
  int priority;
  int enabled;
};

// IAR ISR declaration visible only when the IAR compiler identification macro
// is defined. clang requires __interrupt to be defined away via compat defines
// or it will fail to parse this file and no structs will be found.
#ifdef __IAR_SYSTEMS_ICC__
__interrupt void ADC_Handler(void);
#endif

// CMSIS style: same type defined differently per compiler path.
// if __GNUC__ is not suppressed both blocks are active and clang
// gets a redefinition error causing the whole file to be skipped.
#ifdef __IAR_SYSTEMS_ICC__
typedef unsigned int irq_word_t;
#endif

#ifdef __GNUC__
typedef unsigned long irq_word_t;
#endif

#endif
