/* Copyright (c) 2020 Simon Willcocks */

// This file is the entry point for EL3, it performs initialisation, then drops to Secure EL1

// memset, the only C library function used in the kernel (mostly because the optimiser uses it).
void *memset(void *s, int c, long unsigned int n);

#include "kernel.h"
#include "exclusive.h"
#include "kernel_translation_tables.h"

void initialise_shared_isambard_kernel_tables( Core *core0, int cores );

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

void __attribute__(( noreturn, noinline )) c_el3_nommu( Core *core, unsigned number )
{
  // Running at EL3, without memory management, which means load/store exclusive is inactive
#ifdef QEMU
  asm volatile ( "msr cntfrq_el0, %[bits]\n" : : [bits] "r" (625000000) ); // Frequency for qemu
#else
  asm volatile ( "msr cntfrq_el0, %[bits]\n" : : [bits] "r" (38400000) ); // Frequency of clock (pi3)
#endif

  // Sets bits:
  //   11: ST Do not trap EL1 accesses of CNTPS_* to EL3
  //   10: RW Lower levels Aarch64
  //    9: SIF Secure Instruction Fetch (only from secure memory)
  //  ~ 7: SMD Secure Monitor Call disable
  //    5,4: res1
  asm volatile ( "\tmsr scr_el3, %[bits]" : : [bits] "r" (0b00000000111000110000) );

  if (sizeof( uint32_t ) != 4 || sizeof( uint64_t ) != 8) {
    asm volatile ( "wfi" );
    __builtin_unreachable();
  }

  // Note: Until the MMU is enabled, exclusive access instructions do not work!

  // Identify special cores; all set their appropriate entry (no MMU, atomic
  // operations, write once, then assume the memory is in use) to either
  // NORMAL, in which case return, or SPECIAL, and do your own thing.
  // Any core can be SPECIAL, including core 0. If none are NORMAL, this
  // routine simply won't continue!
  static core_types *present = 0;
  static bool start_roll_call = false;

  if (number == 0) {
    number_of_cores = read_number_of_cores();

    present = (void*) (core + number_of_cores);

    for (unsigned i = 0; i < number_of_cores; i++) {
      present[i] = WAITING;
    }

    start_roll_call = true;

    asm ( "sev" );
  }
  else {
    while (!start_roll_call) {
      asm ( "wfe" );
    }
  }

  roll_call( present, number );

  // TODO: When processors with more than 64 cores become common, simply
  // expand this array.
  static uint64_t present_bits[1] = { 0 };

  // Every core still here waits for the other cores to fill in their
  // present entries, and the lowest numbered normal core to fill in
  // first_free_page, at which point the present array will never be written
  // again. (A straggler core may perform a read or two, but that's safe.)
  while (first_free_page == 0) {
    bool lowest_normal = true; // As far as I know
    bool all_present = true; // As far as I know
    for (unsigned i = 0; i < number_of_cores && first_free_page == 0; i++) {
      if (present[i] == NORMAL && i < number) lowest_normal = false;
      all_present = all_present && (present[i] != WAITING);
    }

    if (first_free_page == 0 && all_present && lowest_normal) {
      for (unsigned i = 0; i < number_of_cores; i++) {
        if (present[i] == NORMAL) present_bits[i / 64] |= (1ull << (i % 64));
      }

      Core *core0 = core - number;
      initialise_shared_isambard_kernel_tables( core0, number_of_cores );

      first_free_page = (integer_register) present;
    }
  }

  // Note that this memset will fill the whole stack with zeros, but this
  // routine does not return, so there should be no bad effects.
  // But use local variables with care, if at all!
  memset( core, 0, sizeof( Core ) );

if (number != 0) { for (;;) { asm ( "wfi" ); } } // TODO TODO TODO!!
  // Note: the address of the present array will be an offset from _start.
  el3_synchronised_initialise( core, number, present_bits );

  __builtin_unreachable();
}
