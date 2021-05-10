/* Copyright (c) 2021 Simon Willcocks */

// EL3 behaviour option:
// Limits EL3 to kernel memory, as controlled by secure el1.
// Allows switching between secure and non-secure modes, handling partner
// threads.

#include "kernel.h"
#include "kernel_translation_tables.h"
#include "isambard_syscalls.h"

#ifndef ENSTRING
#define ENSTRING2( n ) #n
#define ENSTRING( n ) ENSTRING2( n )
#endif

#define numberof( a ) (sizeof( a ) / sizeof( a[0] ))

const int64_t lomem_bits = (32 * 1024 * 1024 - 1);

#define AARCH64_VECTOR_TABLE_NAME VBAR_EL23

// There SHALL NEVER be a situation when there is anything but an FIQ
// occuring from the same exception level (EL2 or EL3).

// There SHALL NEVER be a situation when EL2 code generates an exception
// (Other than by an SMC instruction.)

// FIQ handling is yet to be implemented, but I anticipate handling it at
// EL3, inline, with one page of associated memory.
// If the GPU interrupt is handled at FIQ, I anticipate reading the pending
// register(s), and writing the less important bits to a mailbox, to cause
// an interrupt on a different core.

// The virtual machine will be 32-bit code, so there's no need to store a
// 64-bit SP.


// Interrupts from non-secure mode will copy spsr_el3 and elr_el3 into the
// corresponding _el1 registers, and drop to secure el1, which will store
// the full context as it it were a normal driver thread.

// No interrupts will be routed to EL2.

// Synchronous exceptions will include access to (not-present) peripherals,
// etc. These will be reported to the secure partner thread, which can emulate
// the functionality, as it is scheduled in place of the non-secure thread.

// Switching to and from secure mode requires storing the EL1 translation table
// configuration.
// What, and where?
// TTBR0_EL1, TTBR1_EL1, ..., and in core.

#include "raw/gpio4_led.h"

static uint32_t *const mapped_address = (void*) (16 << 20);

static const uint32_t vwidth = 1920;

#include "raw/trivial_display.h"

uint64_t c_bsod_regs[34] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };

extern void invalidate_all_caches();

static void show_thread( thread_context *thread, uint32_t x, uint32_t colour )
{
  thread = (void *) (((uint64_t) thread) & lomem_bits);

  show_qword( x, 100, (uint64_t) thread, colour );
  invalidate_all_caches();
  for (int i = 0; i < 31; i++) { show_qword( x, 120+20*i, thread->regs[i], colour ); }

  show_qword( x, 800, thread->prev, colour );
  show_qword( x, 810, thread->next, colour );
  show_qword( x, 820, thread->partner, colour );

  show_qword( x, 840, thread->pc, colour );
  show_word( x, 850, thread->spsr, colour );
  show_word( x, 860, thread->gate, colour );
}

void c_bsod()
{
  // Can't store x1 using x1
  asm ( "adr x1, c_bsod_regs"
  "\n  stp x0, xzr, [x1], #16"
  "\n  ldr x0, [sp, #8] // Return address"
  "\n  str x0, [x1, #-8]"
  "\n  stp x2, x3, [x1], #16"
  "\n  stp x4, x5, [x1], #16"
  "\n  stp x6, x7, [x1], #16"
  "\n  stp x8, x9, [x1], #16"
  "\n  stp x10, x11, [x1], #16"
  "\n  stp x12, x13, [x1], #16"
  "\n  stp x14, x15, [x1], #16"
  "\n  stp x16, x17, [x1], #16"
  "\n  stp x18, x19, [x1], #16"
  "\n  stp x20, x21, [x1], #16"
  "\n  stp x22, x23, [x1], #16"
  "\n  stp x24, x25, [x1], #16"
  "\n  stp x26, x27, [x1], #16"
  "\n  stp x28, x29, [x1], #16"
  "\n  ldr x0, [sp, #40] // runnable?"
  "\n  stp x30, x0, [x1], #16"
  );
  invalidate_all_caches();

#ifdef QEMU
static uint32_t *const screen_address = (void*) 0x3c200000;
#else
static uint32_t *const screen_address = (void*) 0x0e400000;
#endif

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
  asm ( "dsb sy" );

  for (int i = 0; i < 34; i++) { show_qword( 200, 120+20*i, c_bsod_regs[i], White ); }
  thread_context *runnable;
  asm ( "mov %[t], sp\norr %[t], %[t], #0xff0\nldr %[t], [%[t],#8]"
        : [t] "=&r" (runnable) );

  runnable = (void *) (((uint64_t) runnable) & lomem_bits);
  invalidate_all_caches();
  show_thread( runnable, 400, Green );
  invalidate_all_caches();
  show_thread( runnable->partner, 600, Yellow );

  int y = 120;
#define show( reg ) { uint64_t r; asm ( "mrs %[r], "#reg : [r] "=r" (r) ); show_qword( 10, y, r, White ); y += 20; }
//show( cntkctl_el1 );
//show( csselr_el1 );
//show( mair_el1 );
//show( sctlr_el1 );
//show( tcr_el1 );
//show( ttbr0_el1 );
//show( ttbr1_el1 );
//show( vbar_el1 );
show( hcr_el2 );
show( vttbr_el2 );
show( hstr_el2 );
show( vmpidr_el2 );
show( vpidr_el2 );
y += 10;
show( vtcr_el2 );
show( hpfar_el2 );
show( ifsr32_el2 );
show( isr_el1 );
y += 20;
show( far_el1 );
show( elr_el1 );
show( esr_el1 );
show( spsr_el1 );
show( sctlr_el1 );
y += 2;
show( ttbr0_el1 );
show( ttbr1_el1 );
y += 2;
show( sp_el1 );
show( vbar_el1 );
y += 20;
show( far_el2 );
show( elr_el2 );
show( esr_el2 );
show( spsr_el2 );
show( sctlr_el2 );

  uint64_t currentel;
  asm ( "mrs %[r], CurrentEL" : [r] "=r" (currentel) );

  if (currentel == 0xc) {
show( sp_el2 );
show( vbar_el2 );
y += 20;
show( far_el3 );
show( elr_el3 );
show( esr_el3 );
show( spsr_el3 );
show( sctlr_el3 );
show( vbar_el3 );
y += 10;
show( scr_el3 );
  }

// contextidr_el2, CPTR_EL2, DACR32_EL2, HACR_EL2, RMR_EL2, RMR_EL2, TPIDR_EL2; No use for these registers
// ESR_EL2, FAR_EL2, HPFAR_EL2, IFSR32_EL2; passed to partner thread to inform of exceptions
// sctlr_el2, tcr_el2, mair_el2, vbar_el2; Relates to Isambard VM implementation, doesn't change

  invalidate_all_caches();
  for (;;) { }
}

// Uses x3, x4, x5
#define SAVE_SYSTEM_REGISTER_PAIR( name1, name2 ) \
    "\n  mrs x4, "#name1 \
    "\n  mrs x5, "#name2 \
    "\n  stp x4, x5, [x3], #16"

// Uses x2, x3, x4, x5
#define SAVE_VM_SYSTEM_REGS \
    asm ( \
    "\n  adr x3, vm" \
    "\n  mrs x2, vttbr_el2" \
    "\n  mov x4, x2, lsr#48" \
    "\n  cbz x4, bsod" \
    "\n  cmp x4, #%[vmmax]" \
    "\n  b.ge bsod" \
    "\n  mov x5, #%[vmsize]" \
    "\n  madd x3, x4, x5, x3" \
    "\n" \
    SAVE_SYSTEM_REGISTER_PAIR( cntkctl_el1, csselr_el1 ) \
    SAVE_SYSTEM_REGISTER_PAIR( mair_el1, sctlr_el1 ) \
    SAVE_SYSTEM_REGISTER_PAIR( tcr_el1, ttbr0_el1 ) \
    SAVE_SYSTEM_REGISTER_PAIR( ttbr1_el1, vbar_el1 ) \
    "\n  mrs x5, hcr_el2" \
    "\n  stp x2, x5, [x3], #16" \
    SAVE_SYSTEM_REGISTER_PAIR( hstr_el2, vmpidr_el2 ) \
    SAVE_SYSTEM_REGISTER_PAIR( vpidr_el2, vtcr_el2 ) \
    SAVE_SYSTEM_REGISTER_PAIR( dacr32_el2, contextidr_el1 ) \
    : \
    : [vmmax] "i" (numberof( vm )) \
    , [vmsize] "i" (sizeof( vm[0] )) \
    );

// Uses x3, x4, x5 // TODO use the offsets into the structure, and check they're name2 follows name1
#define LOAD_SYSTEM_REGISTER_PAIR( name1, name2 ) \
    "\n  ldp x4, x5, [x3], #16" \
    "\n  msr "#name1 ", x4"  \
    "\n  msr "#name2 ", x5"

// Expects x4 to be number of vm (> 0, < numberof( vm ))
// Uses x3, x4, x5
#define LOAD_VM_SYSTEM_REGS \
    asm ( \
    "\n  adr x3, vm" \
    "\n  cbz x4, bsod" \
    "\n  cmp x4, #%[vmmax]" \
    "\n  b.ge bsod" \
    "\n  mov x5, #%[vmsize]" \
    "\n  madd x3, x4, x5, x3" \
    "\n" \
    LOAD_SYSTEM_REGISTER_PAIR( cntkctl_el1, csselr_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( mair_el1, sctlr_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( tcr_el1, ttbr0_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( ttbr1_el1, vbar_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( vttbr_el2, hcr_el2 ) \
    LOAD_SYSTEM_REGISTER_PAIR( hstr_el2, vmpidr_el2 ) \
    LOAD_SYSTEM_REGISTER_PAIR( vpidr_el2, vtcr_el2 ) \
    LOAD_SYSTEM_REGISTER_PAIR( dacr32_el2, contextidr_el1 ) \
    : \
    : [vmmax] "i" (numberof( vm )) \
    , [vmsize] "i" (sizeof( vm[0] )) \
    );

// Uses x2, x3, x4, x5
// Expects x1 -> core->core
#define LOAD_SECURE_EL1_REGS \
    asm ( \
    "\n  adr x3, vm" \
    LOAD_SYSTEM_REGISTER_PAIR( cntkctl_el1, csselr_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( mair_el1, sctlr_el1 ) \
    "\n  // LOAD_SYSTEM_REGISTER_PAIR( tcr_el1, ttbr0_el1 )" \
    "\n  // TODO: See if separating the following two lines from the msr and each other affects speed" \
    "\n  // Get the physical address of the current core structure, and add the offset to the TT" \
    "\n  ldp x4, x5, [x3], #16" \
    "\n  add x2, x1, #16 - %[core_size] + %[pa_offset]" \
    "\n  ldr x5, [x2]" \
    "\n  add x5, x5, #%[tt_l1_offset]" \
    "\n  msr tcr_el1, x4" \
    "\n  msr ttbr0_el1, x5" \
    LOAD_SYSTEM_REGISTER_PAIR( ttbr1_el1, vbar_el1 ) \
    : \
    : [core_size] "i" (sizeof( Core )) \
    , [pa_offset] "i" (&((Core *)0)->physical_address) \
    , [tt_l1_offset] "i" (&((Core *)0)->core_tt_l1) \
    );

// We can safely use the section of the table that has to do with same-level exceptions using SP0,
// since we never use those modes.
#define AARCH64_VECTOR_TABLE_NEVER_SP0

// x1 points to where core and runnable are stored (himem address)

#define AARCH64_VECTOR_TABLE_NEVER_SP0_CODE asm ( \
    "\nin_el3: // x1 points to core, runnable, original x0-x3 are stacked" \
\
    "\n  mrs x2, esr_el3" \
    "\n  mov x3, #0x5e000000" \
    "\n  cmp x3, x2" \
    "\n  b.ne bsod" \
    "\n  // Toggle security state, IRQ, FIQ routing" \
    "\n  mrs x3, scr_el3" \
    "\n  eor x3, x3, #0x007 // FIQ. IRQ, NS" \
    "\n  eor x3, x3, #0x100 // HCE" \
    "\n  msr scr_el3, x3" \
    "\n  tbz x3, #0, switch_to_secure" \
\
    "\n  add sp, sp, #32 // stacked registers not needed" \
    "\n  mov x4, #1 // FIXME: only one VM supported, need to get required VMID to this code" \
    ); \
    LOAD_VM_SYSTEM_REGS \
    asm ( \
    "\n  // Drop straight to non-secure EL < 2, skipping EL2" \
    "\n  ldr x0, [x1, #8]" \
    "\n  and x0, x0, #%[lomem_bits]" \
    "\n  ldp x2, x3, [x0, #%[pc]] // Includes never-used gate value" \
    "\n  msr elr_el3, x2" \
    "\n  msr spsr_el3, x3" \
    "\n" \
    load_pair( x0, 2, 3 ) \
    load_pair( x0, 4, 5 ) \
    load_pair( x0, 6, 7 ) \
    load_pair( x0, 8, 9 ) \
    load_pair( x0, 10, 11 ) \
    load_pair( x0, 12, 13 ) \
    load_pair( x0, 14, 15 ) \
    load_pair( x0, 16, 17 ) \
    load_pair( x0, 18, 19 ) \
    load_pair( x0, 20, 21 ) \
    load_pair( x0, 22, 23 ) \
    load_pair( x0, 24, 25 ) \
    load_pair( x0, 26, 27 ) \
    load_pair( x0, 28, 29 ) \
    "\n  ldr x30, [x0, #%[regs] + 30 * 8]" \
    load_pair( x0, 0, 1 ) \
    "\n  eret" \
    : : \
        [regs] "i" (&((thread_context*)0)->regs), \
        [pc] "i" (&((thread_context*)0)->pc), \
        [lomem_bits] "i" (lomem_bits) \
    ); \
    asm ( \
    "\nrestore_secure_system_regs:" \
    ); \
    SAVE_VM_SYSTEM_REGS \
    LOAD_SECURE_EL1_REGS \
    asm ( \
    "\n  ret" \
    );

#define AARCH64_VECTOR_TABLE_SPX_SYNC_CODE asm ( "bl bsod" ); \
    asm ( \
    "\n// NOT PART OF THE SPX_SYNC_CODE!" \
    "\nbsod: // We've had it, run some C code which never returns" \
    "\n  msr DAIFSet, #0x3" \
    "\n  mov x1, sp" \
    "\n  orr x1, x1, #0xff0" \
    "\n  mov sp, x1" \
    "\n  b c_bsod" );

#define AARCH64_VECTOR_TABLE_SPX_IRQ_CODE asm ( "bl bsod" );
#define AARCH64_VECTOR_TABLE_SPX_FIQ_CODE asm ( "bl bsod" );
#define AARCH64_VECTOR_TABLE_SPX_SERROR_CODE asm ( "bl bsod" \
    "\nswitch_to_secure:" \
    "\n  stp x4, x5, [sp, #-16]! // Push x4 and x5 as well" \
\
    "\n  // Switch to secure mode, and partner thread" \
    "\n  mov x0, x30" \
    "\n  bl restore_secure_system_regs" \
    "\n  mov x30, x0" \
\
    "\n  mrs x2, elr_el2" \
    "\n  msr elr_el1, x2" \
    "\n  mrs x3, spsr_el2" \
    "\n  msr spsr_el1, x3" \
    "\n  // Make a switch to partner call on secure EL1" \
    "\n  mov x0, #0x56000000" \
    "\n  movk x0, #"ENSTRING( ISAMBARD_SWITCH_TO_PARTNER ) \
    "\n  msr esr_el1, x0" \
\
    "\n  mrs x0, vbar_el1" \
    "\n  add x0, x0, #VBAR_EL23_LOWER_AARCH64_SYNC - VBAR_EL23" \
    "\n  msr elr_el3, x0" \
\
    "\n  mov x0, #0x3c5" \
    "\n  msr spsr_el3, x0" \
    "\n  ldp x4, x5, [sp], #16 // Pop x4, x5" \
\
    "\n  ldp x2, x3, [sp, #16] // Pop x0-3 pushed before calling in_el3" \
    "\n  ldp x0, x1, [sp], #32" \
\
    "\n  eret" );

// Store a pair of registers, the low value should be even
#define store_pair( thread, low, high ) "\n  stp x"#low", x"#high", ["#thread", #%[regs] + "#low" * 8]"
#define load_pair( thread, low, high ) "\n  ldp x"#low", x"#high", ["#thread", #%[regs] + "#low" * 8]"

// Note: the lomem_bits line would be better with an add instruction, but
// the constant is then out of range

#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE \
  asm ( \
    "\n  stp x0, x1, [sp, #-32]!" \
    "\n  stp x2, x3, [sp, #16]" \
\
    "\n  mov x1, sp" \
    "\n  orr x1, x1, #0xff0 // x1 points to core->core, core->runnable (sp is always 16-byte aligned)" \
    "\n  dc ivac, x1 // Ensure access to runnable" \
\
    "\n  mrs x0, CurrentEL" \
    "\n  tbnz x0, #2, in_el3" \
\
    "\n  // In EL2: store details, and fake a switch to partner call at secure EL1, via EL3" \
    "\n  ldr x2, [x1, #8] // core->runnable (himem)" \
    "\n  and x2, x2, #%[lomem_bits]" \
\
    "\n  add x1, x2, #%[partner]" \
    "\n  dc ivac, x1 // Ensure access to partner value is up to date" \
    "\n  ldr x1, [x2, #%[partner]] // x0 = lowmem address of non-secure thread, x1 = secure partner" \
    "\n  and x1, x1, #%[lomem_bits] // lowmem address of secure partner" \
    "\n  add x1, x1, #%[regs]" \
    "\n  dc ivac, x1 // Without this, the registers aren't updated" \
    "\n  mrs x2, elr_el2" \
    "\n  mrs x3, esr_el2" \
    "\n  stp x2, x3, [x1]" \
    "\n  mrs x2, far_el2" \
    "\n  mrs x3, hpfar_el2" \
    "\n  stp x2, x3, [x1, #16]" \
    "\n  dc civac, x1 // Without this, the registers aren't updated" \
\
    "\n  ldp x2, x3, [sp, #16]" \
    "\n  ldp x0, x1, [sp], #32" \
\
    "\n  smc #0 // Ask EL3 to switch to partner" \
    : : \
        [partner] "i" (&((thread_context*)0)->partner), \
        [regs] "i" (&((thread_context*)0)->regs), \
        [lomem_bits] "i" (lomem_bits) \
    );


#define REDIRECT_INTERRUPT_TO_SECURE_EL1 \
  asm ( "0:" \
    "\n  stp x0, x1, [sp, #-48]!" \
    "\n  stp x2, x3, [sp, #16]" \
    "\n  stp x4, x5, [sp, #32]" \
    "\n  mov x1, sp" \
    "\n  orr x1, x1, #0xff0 // x1 points to core->core, core->runnable (sp is always 16-byte aligned)" \
\
    "\n  mov x0, x30" \
    "\n  bl restore_secure_system_regs" \
    "\n  mov x30, x0" \
    "\n  mrs x0, elr_el3" \
    "\n  msr elr_el1, x0" \
    "\n  mrs x0, spsr_el3" \
    "\n  msr spsr_el1, x0" \
\
    "\n  mrs x0, vbar_el1" \
    "\n  add x0, x0, #0b - VBAR_EL23" \
    "\n  msr elr_el3, x0" \
\
    "\n  // Toggle security state, IRQ, FIQ routing" \
    "\n  mrs x0, scr_el3" \
    "\n  eor x0, x0, #0x007 // FIQ. IRQ, NS" \
    "\n  eor x0, x0, #0x100 // HCE" \
    "\n  msr scr_el3, x0" \
    "\n  mov x0, #0x3c5" \
    "\n  msr spsr_el3, x0" \
    "\n  ldp x4, x5, [sp, #32]" \
    "\n  ldp x2, x3, [sp, #16]" \
    "\n  ldp x0, x1, [sp], #48" \
    "\n  eret" );

#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_IRQ_CODE REDIRECT_INTERRUPT_TO_SECURE_EL1
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_FIQ_CODE REDIRECT_INTERRUPT_TO_SECURE_EL1
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SERROR_CODE asm ( "bl bsod" );

// This probably never comes into EL3
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SYNC_CODE AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE 
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_IRQ_CODE REDIRECT_INTERRUPT_TO_SECURE_EL1
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_FIQ_CODE REDIRECT_INTERRUPT_TO_SECURE_EL1
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SERROR_CODE asm ( "bl bsod" );

#include "aarch64_vector_table.h"

void __attribute__(( noreturn )) el3_with_mmu( EL_PARAMETERS )
{
  el3_prepare_el2_for_entry( core );
  el3_run_at_secure_el1( EL_ARGUMENTS, isambard_secure_el1 );
}

void __attribute__(( noreturn )) el3_synchronised_initialise( EL_PARAMETERS )
{
  // To enable virtual machines:
  //  EL3 has to accept requests to switch between secure and non-secure modes.
  //  EL2 has to store and restore (partner) thread states
  //  EL2 has to have access to the kernel structures
  //  Both EL2 and EL3 should run from cached memory
  //  Both EL2 and EL3 need a (small) stack, per core.
  // If EL2 is entered from a lower EL, the register state is stored in the
  // current thread, and the details of the exception in the partner thread's
  // register, for when it's re-started.
  // If EL2 is entered from EL3, x0 refers to the state that should be restored
  // Secure EL1 can request a mode switch by executing SMC #1
  // Non-Secure EL2 can request a mode switch with SMC #2

  core->physical_address = core; // For passing to MMU, EL1 (without MMU), etc.

  el3_run_with_mmu( EL_ARGUMENTS, el3_with_mmu );
  __builtin_unreachable();
}

void roll_call( core_types *present, unsigned number )
{
  // EL2 and EL3 are a simple veneer to switch between Secure and Non-Secure
  // No need for separate tables.
  asm volatile ( "  msr VBAR_EL3, %[table]\n" : : [table] "r" (VBAR_EL23) );
  asm volatile ( "  msr VBAR_EL2, %[table]\n" : : [table] "r" (VBAR_EL23) );

  uint64_t hcr2 = 0b1000001110000000000000011111110110000111011;
  asm volatile ( "  msr HCR_EL2, %[bits]\n" : : [bits] "r" (hcr2) );

  asm volatile ( "  msr VPIDR_EL2, %[bits]\n" : : [bits] "r" (0x410fb767) ); // ARM1176JZ-S

  led_init( 0x3f200000 );

  present[number] = NORMAL;
}
