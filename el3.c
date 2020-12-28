/* Copyright (c) 2020 Simon Willcocks */

// memset, the only C library function used in the kernel (mostly because the optimiser uses it).
void *memset(void *s, int c, unsigned long long n);

#include "kernel.h"
#include "exclusive.h"

uint8_t initial_first_free_page; // Not a real variable, its location is set by the linker
integer_register volatile number_of_cores = 0;
integer_register volatile first_free_page = 0;

static inline int read_number_of_cores()
{
  int res;
  // L2CTLR_EL1:
  asm volatile( "\tMRS %[result], S3_1_C11_C0_2" : [result] "=r" (res) );
  return ((res >> 24) & 0x3) +1;
}

void __attribute__(( noreturn, noinline )) c_el3_nommu( Core *core, int number )
{
  if (number == 0) {
    // Only one core should do this, and there is always a core 0.
    number_of_cores = read_number_of_cores();
    first_free_page = (integer_register) (first_free_page + number_of_cores * sizeof( Core ));
  }

  // Note that this memset will fill the whole stack with zeros, but this routine does
  // not return, so there should be no bad effects.
  // But use local variables with care, if at all!
  memset( core, 0, sizeof( Core ) );

  while (first_free_page == 0 || number_of_cores == 0) {}
  // The variables have been initialised by core 0

  for (;;) { } // This is where you do stuff, without returning

  __builtin_unreachable();
}
