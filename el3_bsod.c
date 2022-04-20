/* Copyright 2022 Simon Willcocks */

/* Show information about the current core state. */

#include "kernel.h"
#include "kernel_translation_tables.h"


#include "raw/gpio4_led.h"

static uint32_t *const mapped_address = (void*) (16 << 20);

static const uint32_t vwidth = 1920;

#include "raw/trivial_display.h"

extern void invalidate_all_caches();

#ifdef QEMU
static uint32_t *const screen_address = (void*) 0x3c200000;
#else
static uint32_t *const screen_address = (void*) 0x0e400000;
#endif

void map_screen()
{
  extern Aarch64_VMSA_entry kernel_tt_l2[16];
  for (int i = 8; i < 12; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( ((i - 8) << 21) + (uint64_t) screen_address );
    entry = Aarch64_VMSA_el0_rw_( entry );
    entry.access_flag = 1; // Don't want to be notified when accessed
    entry.shareability = 3; // Inner shareable
    entry = Aarch64_VMSA_global( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    kernel_tt_l2[i] = entry;
  }
  asm( "dmb sy" );
}

void map_2MB( uint32_t physical, uint32_t virtual )
{
  extern Aarch64_VMSA_entry kernel_tt_l2[16];
  Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( ((physical >> 21)<<21) );
  entry = Aarch64_VMSA_el0_rw_( entry );
  entry.access_flag = 1; // Don't want to be notified when accessed
  entry.shareability = 3; // Inner shareable
  entry = Aarch64_VMSA_global( entry );
  entry = Aarch64_VMSA_write_back_memory( entry );
  kernel_tt_l2[virtual >> 21] = entry;
  asm( "dmb sy" );
}

void show_vm_regs()
{
  int y = 650;
#define show( reg ) { uint64_t reg; asm ( "mrs %[r], "#reg : [r] "=r" (reg) ); show_qword( 1000, y, reg, White ); y += 10; }
  show( cntkctl_el1 );
  show( csselr_el1 );

  show( mair_el1 );
  show( sctlr_el1 );

  show( tcr_el1 );
  show( ttbr0_el1 ); // Core-specific, in secure mode

  show( ttbr1_el1 );
  show( vbar_el1 );

  show( actlr_el1 );

y+=4;
  show( spsr_el1 );
  show( elr_el1 );
  show( spsr_el2 );
  show( elr_el2 );

y+=4;
  show( esr_el1 );
  show( far_el1 );

  show( esr_el2 );
  show( far_el2 );

y+=4;
  show( vttbr_el2 );
  show( hcr_el2 );
  show( hstr_el2 );
  show( vmpidr_el2 );
  show( vpidr_el2 );
  show( vtcr_el2 );
  show( dacr32_el2 );
  show( contextidr_el1 );

  uint32_t el;
  asm ( "mrs %[el], CurrentEL" : [el] "=r" (el) );
  if ((el >> 2) > 2) {
    y+=4;
    show( spsr_el3 );
    show( elr_el3 );
    show( esr_el3 );
    show( far_el3 );
  }

#undef show
}

void internal_c_bsod( uint64_t *regs )
{
  invalidate_all_caches();
  map_screen();
  for (int i = 0; i < 32; i++) { show_qword( 100, 615+10*i, regs[i], 0xffffff00 ); }
  show_vm_regs();
  invalidate_all_caches();
  for (;;) { asm ( "wfi" ); }
}

asm (
    ".global c_bsod"
  "\n.type c_bsod, function"
  "\nc_bsod:"
  "\n  stp x30, x30, [sp, #-16]!"
  "\n  stp x28, x29, [sp, #-16]!"
  "\n  stp x26, x27, [sp, #-16]!"
  "\n  stp x24, x25, [sp, #-16]!"
  "\n  stp x22, x23, [sp, #-16]!"
  "\n  stp x20, x21, [sp, #-16]!"
  "\n  stp x18, x19, [sp, #-16]!"
  "\n  stp x16, x17, [sp, #-16]!"
  "\n  stp x14, x15, [sp, #-16]!"
  "\n  stp x12, x13, [sp, #-16]!"
  "\n  stp x10, x11, [sp, #-16]!"
  "\n  stp x8, x9, [sp, #-16]!"
  "\n  stp x6, x7, [sp, #-16]!"
  "\n  stp x4, x5, [sp, #-16]!"
  "\n  stp x2, x3, [sp, #-16]!"
  "\n  stp x0, x1, [sp, #-16]!"
  "\n  mov x0, sp"
  "\n  b internal_c_bsod" );

