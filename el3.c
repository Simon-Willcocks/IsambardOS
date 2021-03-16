/* Copyright (c) 2020 Simon Willcocks */

// This file is the entry point for EL3, it performs initialisation, then drops to Secure EL1

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

extern void setup_el3_for_reentry( int number );

typedef void __attribute__(( noreturn )) (*secure_el1_code)( Core *core, int number, uint64_t volatile *present );

static void __attribute__(( noreturn )) run_at_secure_el1( Core *core, int number, uint64_t volatile *present, secure_el1_code code )
{
  core->runnable = 0;
  core->core = 0;

  asm volatile ( "\tmsr sp_el1, %[SP]"
               "\n\tmov sp, %[SP]"
               "\n\tmsr spsr_el3, %[PSR]"
               "\n\tmsr elr_el3, %[EL1]"
               "\n\tmov x0, %[CORE]"
               "\n\tmov x1, %[NUMBER]"
               "\n\tmov x2, %[PRESENT]"
               "\n\teret"
               :
               : [SP] "r" (&core->core) // So core and runnable aren't overwritten as stack
               , [EL1] "r" (code)
               , [CORE] "r" (core)
               , [NUMBER] "r" (number)
               , [PRESENT] "r" (present)
               , [PSR] "r" (0xc5) // EL1, using SP_EL1, interrupts disabled
               : "x0", "x1", "x2" );

  __builtin_unreachable();
}

extern void __attribute__(( noreturn )) enter_secure_el1( Core *phys_core, int number, uint64_t volatile *present );

void __attribute__(( noreturn, noinline )) c_el3_nommu( Core *core, int number )
{
  // Running at EL3, without memory management, which means load/store exclusive is inactive

  // asm volatile ( "msr cntfrq_el0, %[bits]\n" : : [bits] "r" (0x0124f800) ); // Frequency 19.2 MHz
  asm volatile ( "msr cntfrq_el0, %[bits]\n" : : [bits] "r" (38400000) );
  // asm volatile ( "msr cntfrq_el0, %[bits]\n" : : [bits] "r" (1000000) ); // Frequency 1 MHz

  if (sizeof( uint32_t ) != 4 || sizeof( uint64_t ) != 8) {
    asm volatile ( "wfi" );
    __builtin_unreachable();
  }

  static uint64_t volatile present = 0;

  if (number == 0) {
    // Only one core should do this, and there is always a core 0.
    number_of_cores = read_number_of_cores();
    first_free_page = (integer_register) (core + number_of_cores);

    // Until I can sort out a no-exclusive-operators bit manipulation, there're all here...
    present = (1 << number_of_cores)-1;

    asm volatile ( "dsb sy" );
  }

  // Note that this memset will fill the whole stack with zeros, but this routine does
  // not return, so there should be no bad effects.
  // But use local variables with care, if at all!
  memset( core, 0, sizeof( Core ) );

  while (first_free_page == 0 || number_of_cores == 0) {}
  // The variables have been initialised by core 0

  setup_el3_for_reentry( number );

  run_at_secure_el1( core, number, &present, enter_secure_el1 );

  __builtin_unreachable();
}
