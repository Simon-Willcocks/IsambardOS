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


typedef void __attribute__(( noreturn )) (*secure_el1_code)( Core *core, int number, uint64_t volatile *present );

static void __attribute__(( noreturn )) run_at_secure_el1( Core *core, int number, uint64_t volatile *present, secure_el1_code code )
{
  asm volatile ( "\tmsr sp_el1, %[SP]"
               "\n\tmov sp, %[SP]"
               "\n\tmsr spsr_el3, %[PSR]"
               "\n\tmsr elr_el3, %[EL1]"
               "\n\tmov x0, %[CORE]"
               "\n\tmov x1, %[NUMBER]"
               "\n\tmov x2, %[PRESENT]"
               "\n\teret"
               :
               : [SP] "r" (core+1)
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

  static uint64_t volatile resume = 0;
  static uint64_t volatile present = 0;

  if (number == 0) {
    resume = (1ull << number_of_cores) - 1;
    // In subsequent code segments, use resume = present;
  }
  else {
    // Wait for resume to be initialised
    while (resume == 0) {}
  }

  switch (number) {
#ifdef EL3_RAW_DISPLAY
  case EL3_RAW_DISPLAY:
    {
extern void __attribute__(( noreturn )) el3_no_mmu_display_pages( Core *core );

      clearbit( &resume, number );
      el3_no_mmu_display_pages( core );
      // (This core will not be "present", for the rest of the routine.)
    }
    break;
#endif
  default:
    {
      // Insert code that will not block forever, here
      setbit( &present, number );
      clearbit( &resume, number );
    }
    break;
  }

  while (resume != 0) {}
  // All the cores have executed the preceding switch, by this point,
  // or are never going to return from it. The ones that did return
  // have set a bit in present, which can be used to initialise resume
  // for the next sync.
  run_at_secure_el1( core, number, &present, enter_secure_el1 );

  __builtin_unreachable();
}
