// This file is intended to be used to set up a vector table that will enter a C function when an exception occurs.
// Any exception's code can be overridden by defining the macro expected by aarch64_vector_table.h before including
// this file.
// Before including this file, define AARCH64_VECTOR_TABLE_PREFIX, HANDLER_EL (1, 2, or 3) and
// AARCH64_VECTOR_TABLE_NAME.
//
// FIQ handlers MUST NOT use the C calling code, because it expects the stack to be just so on entry and FIQs can
// occur during IRQ handling (to minimise latency).
// Synchronous exceptions in handlers will also break the mechanism.
// In those cases, this file defaults to BL to the routine incompatible_event - this routine can only report the
// fatal error, not correct it.
//
// An alternative method could be used (one vector table per core, with the context and thread pointers stored
// at a known location), or (slower, obviously), read the core number and look up the appropriate pointers.
//
// I've chosen not to do that because:
//   1. The purpose of the FIQ mechanism is to be Fast; the code can use a bit of stack and values stored in global
//   variables to do its job, then trigger a normal interrupt to pick up the data as needed.
//   2. The amount of code at the handler level will be as small and correct as possible; pass any memory allocations
//   off to helper threads.
//
// To provide FIQ code, define AARCH64_VECTOR_TABLE_FIQ_CODE, which will be used in all four FIQ events.
//
// Note: The aarch64 architecture demands that the handlers use the stack, you can't store to a pc relative location.

typedef unsigned long long u64;

/* Files including this header must first declare a type that is a superset of this:
typedef struct __attribute__(( packed )) {
  // Anything added here is unaffected by the code in this header.
  // The following 4 elements (35, 8-byte words) must remain in this order.
  u64 regs[31];
  u64 sp;
  u64 pc;
  u64 spsr;
  // Anything added here is unaffected by the code in this header.
} thread_context;
*/

typedef struct __attribute__(( packed )) {
  void *opaque;
  thread_context *thread;
} stack_content;

typedef struct __attribute__(( packed )) {
  thread_context *now;
  thread_context *then;
} thread_switch;

// On an exception, all registers clobbered by an aapcs64 function call will have been stored in the
// thread_context pointed to by the last pointer pushed onto the stack.
// The appropriate C function will be called, passing in the pointer to the thread_context, and
// the next pointer up the stack.
// On return, the C routine must return a threads structure (by value), with the original thread_context "then"
// and the thread_context "now" for the thread to be resumed.
// If the thread is unchanged, the aapcs64 resisters will be restored from the thread_context, and the
// execution resumed at the interrupted instruction.
// If the thread is changed ("now" != "then"), the remaining registers will be stored in the "then" context, and all
// registers loaded from the "now" context before resuming execution at the previous execution level.

#define offsetof( type, member ) ((u64) (char*) (&((type*) 0ULL)->member))
#define store_pair( r1, r2 ) asm volatile ( "\n\tstp x"#r1", x"#r2", [x1, #%[offset]]" : : [offset] "i" (offsetof( thread_context, regs[r1])) );
#define load_pair( r1, r2 ) asm volatile ( "\n\tldp x"#r1", x"#r2", [x0, #%[offset]]" : : [offset] "i" (offsetof( thread_context, regs[r1])) );

#define JOIN( p, e ) p##e
#define STANDARD_HANDLER( prefix, event ) thread_switch JOIN( prefix, event )( void *opaque, thread_context *context )

#ifndef MACRO_AS_STRING
#define MACRO_AS_STRING2( number ) #number
#define MACRO_AS_STRING( number ) MACRO_AS_STRING2( number )
#endif

#define C_HANDLER( event ) \
  asm volatile ( "\tmsr DAIFClr, #0x1" ); /* Enable FIQ immediately */ \
  asm volatile ( "\n\tstp x0, x1, [sp, #-16]!" ); \
  \
  asm volatile ( "\n\tldp x0, x1, [sp, #16]" ); \
  store_pair( 2, 3 ); \
  asm volatile ( "\tldp x2, x3, [sp], #16" ); \
  asm volatile ( "\tstp x2, x3, [x1, #%[offset]]" : : [offset] "i" (offsetof( thread_context, regs[0] )) ); \
  asm volatile ( "\tmrs x2, ELR_EL" MACRO_AS_STRING( HANDLER_EL ) ); \
  asm volatile ( "\tmrs x3, SPSR_EL" MACRO_AS_STRING( HANDLER_EL ) ); \
  asm volatile ( "\tstp x2, x3, [x1, #%[offset]]" : : [offset] "i" (offsetof( thread_context, pc )) ); \
  store_pair( 4, 5 ); \
  store_pair( 6, 7 ); \
  store_pair( 8, 9 ); \
  store_pair( 10, 11 ); \
  store_pair( 12, 13 ); \
  store_pair( 14, 15 ); \
  store_pair( 16, 17 ); \
  asm volatile ( "\tstp x18, x30, [x1, #%[offset]]" : : [offset] "i" (offsetof( thread_context, regs[18] )) ); \
  asm volatile ( "\tbl "MACRO_AS_STRING( AARCH64_VECTOR_TABLE_PREFIX ) #event ); \
  asm volatile ( "\tldp x18, x30, [x1, #%[offset]]" : : [offset] "i" (offsetof( thread_context, regs[18] )) ); \
  asm volatile ( "\tcmp x0, x1\n\tbeq el"  MACRO_AS_STRING( HANDLER_EL ) "_resume_thread" ); \
  store_pair( 18, 19 ); \
  store_pair( 20, 21 ); \
  store_pair( 22, 23 ); \
  store_pair( 24, 25 ); \
  store_pair( 26, 27 ); \
  store_pair( 28, 29 ); \
  asm volatile ( "\n\tmrs x29, sp_el0" ); \
  asm volatile ( "\n\tstp x30, x29, [x1, #%[offset]]" : : [offset] "i" (offsetof( thread_context, regs[30] )) ); \
  asm volatile ( "\tb el"  MACRO_AS_STRING( HANDLER_EL ) "_enter_thread" );

#define INCOMPATIBLE_EVENT asm volatile ( "bl incompatible_event" );

#ifndef AARCH64_VECTOR_TABLE_FIQ_CODE
#define AARCH64_VECTOR_TABLE_FIQ_CODE INCOMPATIBLE_EVENT
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_SYNC_CODE
#define AARCH64_VECTOR_TABLE_SP0_SYNC_CODE C_HANDLER( SP0_SYNC_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, SP0_SYNC_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_IRQ_CODE
#define AARCH64_VECTOR_TABLE_SP0_IRQ_CODE C_HANDLER( SP0_IRQ_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, SP0_IRQ_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_FIQ_CODE
#define AARCH64_VECTOR_TABLE_SP0_FIQ_CODE AARCH64_VECTOR_TABLE_FIQ_CODE
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_SERROR_CODE
#define AARCH64_VECTOR_TABLE_SP0_SERROR_CODE INCOMPATIBLE_EVENT
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_SYNC_CODE
#define AARCH64_VECTOR_TABLE_SPX_SYNC_CODE C_HANDLER( SPX_SYNC_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, SPX_SYNC_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_IRQ_CODE
#define AARCH64_VECTOR_TABLE_SPX_IRQ_CODE C_HANDLER( SPX_IRQ_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, SPX_IRQ_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_FIQ_CODE
#define AARCH64_VECTOR_TABLE_SPX_FIQ_CODE AARCH64_VECTOR_TABLE_FIQ_CODE
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_SERROR_CODE
#define AARCH64_VECTOR_TABLE_SPX_SERROR_CODE INCOMPATIBLE_EVENT
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE C_HANDLER( LOWER_AARCH64_SYNC_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, LOWER_AARCH64_SYNC_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_IRQ_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_IRQ_CODE C_HANDLER( LOWER_AARCH64_IRQ_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, LOWER_AARCH64_IRQ_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_FIQ_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_FIQ_CODE AARCH64_VECTOR_TABLE_FIQ_CODE
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_SERROR_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SERROR_CODE C_HANDLER( LOWER_AARCH64_SERROR_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, LOWER_AARCH64_SERROR_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_SYNC_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SYNC_CODE C_HANDLER( LOWER_AARCH32_SYNC_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, LOWER_AARCH32_SYNC_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_IRQ_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_IRQ_CODE C_HANDLER( LOWER_AARCH32_IRQ_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, LOWER_AARCH32_IRQ_CODE );
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_FIQ_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_FIQ_CODE AARCH64_VECTOR_TABLE_FIQ_CODE
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_SERROR_CODE
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SERROR_CODE C_HANDLER( LOWER_AARCH32_SERROR_CODE )
STANDARD_HANDLER( AARCH64_VECTOR_TABLE_PREFIX, LOWER_AARCH32_SERROR_CODE );
#endif

#include "aarch64_vector_table.h"

#if HANDLER_EL == 1
// Support routines
// Don't make any calls from this routine, we don't want a stack frame
// This routine is used at secure el1, to enter whichever thread is now
// the current thread. The old thread's state has already been completely
// stored.
void __attribute__(( noreturn, noinline, optimize( "-Os" ) )) el1_enter_thread( thread_switch threads )
{
  integer_register spsr = threads.now->spsr;
  integer_register pc = threads.now->pc;
  if (0 != (spsr & 0x10) && 0 != threads.now->partner) {
    // Switch to EL3, to switch to EL2
    asm ( "smc 0" );
  }
  // Load thread.now.pc, thread.now.spsr
  asm volatile ( "msr elr_el1, %[pc]"
             "\n\tmsr spsr_el1, %[spsr]"
		  :
		  : [pc] "r" (pc), [spsr] "r" (spsr) );

  load_pair( 30, 29 ); // x30 & stored stack pointer (into x29)
  asm volatile ( "msr sp_el0, x29" ); // Assuming always sp0, doesn't affect A32 code, stack's in a normal register
  load_pair( 2, 3 );
  load_pair( 4, 5 );
  load_pair( 6, 7 );
  load_pair( 8, 9 );
  load_pair( 10, 11 );
  load_pair( 12, 13 );
  load_pair( 14, 15 );
  load_pair( 16, 17 );
  load_pair( 18, 19 );
  load_pair( 20, 21 );
  load_pair( 22, 23 );
  load_pair( 24, 25 );
  load_pair( 26, 27 );
  load_pair( 28, 29 );
  asm volatile ( "str x0, [sp, #8]" ); // Above where opaque is stored on the exception stack

  load_pair( 0, 1 );

  asm volatile ( "dsb sy" );
  asm volatile ( "eret" );
  __builtin_unreachable();
}

// Don't make any calls from this routine, we don't want a stack frame
// Re-load AAPCS64 - corruptable registers
void __attribute__(( noreturn, noinline, optimize( "-Os" ) )) el1_resume_thread( thread_context *thread )
{
  //asm volatile ( ".if 
  asm volatile ( "ldp x2, x3, [%[thread], #%[offset]]"
             "\n\tmsr elr_el1, x2"
             "\n\tmsr spsr_el1, x3"
		  :
		  : [thread] "r" (thread), [offset] "i" (offsetof( thread_context, pc ))
                  : "x2", "x3" );
  load_pair( 2, 3 );
  load_pair( 4, 5 );
  load_pair( 6, 7 );
  load_pair( 8, 9 );
  load_pair( 10, 11 );
  load_pair( 12, 13 );
  load_pair( 14, 15 );
  load_pair( 16, 17 );
  asm volatile ( "str x0, [sp, #8]" ); // Above where opaque is stored on the exception stack

  load_pair( 0, 1 );

  asm volatile( "dsb sy" );
  asm volatile ( "eret" );

  __builtin_unreachable();
}
#endif

