/* Copyright (c) 2021 Simon Willcocks */

// EL3 behaviour option:
// Limits EL3 to kernel memory, as controlled by secure el1.
// Allows switching between secure and non-secure modes, handling partner
// threads.

#include "kernel.h"
#include "kernel_translation_tables.h"
#include "isambard_syscalls.h"

extern void el3_prepare_el2_for_entry( Core *core );

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


// Interrupts from non-secure mode will spsr_el3 and elr_el3 into the
// corresponding _el1 registers, and drop to secure el1, which will store
// the full context as it it were a normal driver thread.

// No interrupts will be routed to EL2.

// Synchronous exceptions will include access to (not-present) peripherals,
// etc. These will be reported to the secure partner thread, which can emulate
// the functionality, as it is scheduled in place of the non-secure thread.

// Switching to and from secure mode requires storing the EL1 translation table
// configuration. This is done at EL3, in memory associated with the partner 
// thread.

extern void c_bsod();


// NOTE!
// In the following VBAR code, there is copious use of "i" inputs to the asm code, but NO "r" or "m" inputs.
// The compiler MUST NOT be allowed to allocate registers itself.


// Uses x3, x4, x5
#define SAVE_SYSTEM_REGISTER_PAIR( name1, name2 ) \
  asm ( \
    "\n  mrs x4, "#name1 \
    "\n  mrs x5, "#name2 \
    "\n  stp x4, x5, [x3, #%[n1off]]" \
    "\n.ifne %[n2off] - %[n1off] - 8" \
    "\n  .error \"Trying to access pair of system variables that aren't consecutive: "#name1", "#name2"\"" \
    "\n.endif" \
    : \
    : [n1off] "i" (&((vm_state *)0)->name1) \
    , [n2off] "i" (&((vm_state *)0)->name2) \
  );

// Uses x2, x3, x4, x5
#define SAVE_VM_SYSTEM_REGS \
    asm ( \
    "\n  ldr x3, [x1, #8]" /* runnable, the non-secure partner thread */ \
    "\n  add x3, x3, %[threadsize]" \
    "\n  and x3, x3, #%[lomem_bits]" \
    : \
    : [threadsize] "i" (sizeof( thread_context )) \
    , [lomem_bits] "i" (lomem_bits) \
    ); \
    SAVE_SYSTEM_REGISTER_PAIR( mair_el1, sctlr_el1 ) \
    SAVE_SYSTEM_REGISTER_PAIR( tcr_el1, ttbr0_el1 ) \
    SAVE_SYSTEM_REGISTER_PAIR( ttbr1_el1, vbar_el1 ) \
    SAVE_SYSTEM_REGISTER_PAIR( actlr_el1, fpexc32_el2 ) \
    SAVE_SYSTEM_REGISTER_PAIR( esr_el1, far_el1 ) \
    SAVE_SYSTEM_REGISTER_PAIR( vttbr_el2, hcr_el2 ) \
    SAVE_SYSTEM_REGISTER_PAIR( hstr_el2, vmpidr_el2 ) \
    SAVE_SYSTEM_REGISTER_PAIR( vpidr_el2, vtcr_el2 ) \
    SAVE_SYSTEM_REGISTER_PAIR( dacr32_el2, contextidr_el1 ) \

// Uses x3, x4, x5
#define LOAD_SYSTEM_REGISTER_PAIR( name1, name2 ) \
  asm ( \
    "\n  ldp x4, x5, [x3, #%[n1off]]" \
    "\n  msr "#name1 ", x4"  \
    "\n  msr "#name2 ", x5" \
    "\n.ifne %[n2off] - %[n1off] - 8" \
    "\n  .error \"Trying to access pair of system variables that aren't consecutive: "#name1", "#name2"\"" \
    "\n.endif" \
    : \
    : [n1off] "i" (&((vm_state *)0)->name1) \
    , [n2off] "i" (&((vm_state *)0)->name2) \
  );

// Expects x0 to point to the VM partner thread (with system registers)
// Uses x3, x4, x5
#define LOAD_VM_SYSTEM_REGS \
    asm ( \
    "\n  add x3, x0, %[threadsize]" \
    : \
    : [threadsize] "i" (sizeof( thread_context )) \
    ); \
    LOAD_SYSTEM_REGISTER_PAIR( mair_el1, sctlr_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( tcr_el1, ttbr0_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( ttbr1_el1, vbar_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( actlr_el1, fpexc32_el2 ) \
    LOAD_SYSTEM_REGISTER_PAIR( esr_el1, far_el1 ) \
    LOAD_SYSTEM_REGISTER_PAIR( vttbr_el2, hcr_el2 ) \
    LOAD_SYSTEM_REGISTER_PAIR( hstr_el2, vmpidr_el2 ) \
    LOAD_SYSTEM_REGISTER_PAIR( vpidr_el2, vtcr_el2 ) \
    LOAD_SYSTEM_REGISTER_PAIR( dacr32_el2, contextidr_el1 ) \

// Uses x2, x3, x4, x5
// Expects x1 -> core->core
#define LOAD_SECURE_EL1_REGS \
    asm ( \
    "\n  adr x3, secure_registers" ); \
    LOAD_SYSTEM_REGISTER_PAIR( mair_el1, sctlr_el1 ) \
    asm ( \
    "\n  // LOAD_SYSTEM_REGISTER_PAIR( tcr_el1, ttbr0_el1 )" \
    "\n  // TODO: See if separating the following two lines from the msr and each other affects speed" \
    "\n  // Get the physical address of the current core structure, and add the offset to the TT" \
    "\n  ldr x4, [x3, #%[n1off]]" \
    "\n  add x2, x1, #16 - %[core_size] + %[pa_offset]" \
    "\n  ldr x5, [x2]" \
    "\n  add x5, x5, #%[tt_l1_offset]" \
    "\n  msr tcr_el1, x4" \
    "\n  msr ttbr0_el1, x5" \
    "\n.ifne %[n2off] - %[n1off] - 8" \
    "\n  .error \"Trying to access pair of system variables that aren't consecutive: tcr_el1, ttbr0_el1\"" \
    "\n.endif" \
    : \
    : [core_size] "i" (sizeof( Core )) \
    , [pa_offset] "i" (&((Core *)0)->physical_address) \
    , [tt_l1_offset] "i" (&((Core *)0)->core_tt_l1) \
    , [n1off] "i" (&((vm_state *)0)->tcr_el1) \
    , [n2off] "i" (&((vm_state *)0)->ttbr0_el1) \
    ); \
    LOAD_SYSTEM_REGISTER_PAIR( ttbr1_el1, vbar_el1 )

// We can safely use the section of the table that has to do with same-level exceptions using SP0,
// since we never use those modes.
#define AARCH64_VECTOR_TABLE_NEVER_SP0

// x1 points to where core and runnable are stored (himem address)

#define AARCH64_VECTOR_TABLE_NEVER_SP0_CODE asm ( "\n" \
    "in_el3: // x1 points to core, runnable, original x0-x3 are stacked" \
\
    "\n  // Toggle security state, IRQ, FIQ routing" \
    "\n  mrs x3, scr_el3" \
    "\n  eor x3, x3, #0x007 // FIQ. IRQ, NS" \
    "\n  eor x3, x3, #0x100 // HCE" \
    "\n  msr scr_el3, x3" \
    "\n  tbz x3, #0, switch_to_secure" \
\
    "\n  add sp, sp, #32 // stacked registers not needed" \
    "\n  ldr x0, [x1, #8]" /* non-secure partner */ \
    "\n  and x0, x0, #%[lomem_bits]" \
      : :  [lomem_bits] "i" (lomem_bits) \
    ); \
    LOAD_VM_SYSTEM_REGS \
    asm ( \
    "\n  // Drop straight to non-secure EL < 2, skipping EL2" \
    "\n  ldp x2, x3, [x0, #%[pc]] // Includes never-used gate value" \
    "\n  msr elr_el3, x2" \
    "\n  msr spsr_el3, x3" \
                                "\n  and x29, x3, #15" \
                                "\n  cmp x29, #0xd" \
                                "\n  bne resume_ns_thread" \
                                "\n  bl bsod" \
    "\n  b resume_ns_thread" \
    : \
    : [pc] "i" (&((thread_context*)0)->pc) \
    ); \
    asm ( \
    "restore_secure_system_regs:" \
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
    "\n  orr sp, x1, #0xff0" \
    "\n  b c_bsod" );

#define AARCH64_VECTOR_TABLE_SPX_IRQ_CODE asm ( "bl bsod" ); \
    asm ( "\n" \
    "resume_ns_thread:" \
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
    : : [regs] "i" (&((thread_context*)0)->regs) \
    );

#define AARCH64_VECTOR_TABLE_SPX_FIQ_CODE asm ( "bl bsod\n" );
#define AARCH64_VECTOR_TABLE_SPX_SERROR_CODE asm ( "bl bsod\n" \
    "switch_to_secure:" \
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
                                    "\n  tbz x3, #4, c_bsod" \
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
    "\n  // These can't be run under secure el0, afaics..." \
    "\n  DSB sy // FIXME: what's really needed?" \
    "\n  ISB sy // FIXME: what's really needed?" \
    "\n  IC IALLU // FIXME: what's really needed?" \
    "\n  TLBI ALLE2 // FIXME: what's really needed?" \
    "\n  TLBI VMALLS12E1IS // FIXME: what's really needed?" \
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

static void watchdog()
{
#ifdef QEMU
static uint32_t *const screen_address = (void*) 0x3c200000;
#else
static uint32_t *const screen_address = (void*) 0x0e400000;
#endif
  int n = 0;
  uint32_t volatile *counter = (void*) 0x04400000;
  *counter = 0x44446666;
  for (;;) {
    uint32_t old_value = *counter;
    for (int i = 0; i < 0x7fffff; i++) { asm ( "" ); };
    uint32_t new_value = *counter;
    screen_address[n++] = (old_value == new_value) ? 0xffff0000 : 0xffffff00;
  }
}


void roll_call( core_types volatile *present, unsigned number )
{
  if (number == 3) {
    // Watchdog core
    present[number] = SPECIAL; // Tell the caller we won't be returning
    watchdog();
  }


  // EL2 and EL3 are a simple veneer to switch between Secure and Non-Secure
  // No need for separate tables.
  asm volatile ( "  msr VBAR_EL3, %[table]\n" : : [table] "r" (VBAR_EL23) );
  asm volatile ( "  msr VBAR_EL2, %[table]\n" : : [table] "r" (VBAR_EL23) );

  // SDD no debug events in secure mode, this means that there's no need to context switch debug
  // registers between Secure and Non-Secure mode.
  asm volatile ( "  msr MDCR_EL3, %[bits]\n" : : [bits] "r" (1 << 16) );
  // ARM DDI 0487G.a, page 2555: to get debug events sent to EL2, the following flags must be set to:
  // Lock FALSE, SCR_EL3.NS 1, MDCR_EL3.SDD x, SCR_EL3.EEL2 x,
  // HCR_EL2.TGE 0, MDCR_EL2.TDE 1,
  // MDSCR_EL1.KDE 0, PSTATE.D x.

  register uint64_t bits;
  // TGE is known to be 0, non-Secure mode requires a guest OS
  asm volatile ( "  mrs %[bits], MDCR_EL2\n" : [bits] "=r" (bits) );
  bits |= (1 << 8);
  asm volatile ( "  msr MDCR_EL2, %[bits]\n" : : [bits] "r" (bits) );

  present[number] = NORMAL;
}
