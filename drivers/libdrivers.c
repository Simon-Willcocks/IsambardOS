#include "drivers.h"

#ifndef ISAMBARD_GATE
#error WUT
#endif

#ifndef STACK_SIZE
#define STACK_SIZE 128
#endif
#define STACK_SIZE_STRINGX(s) #s
#define STACK_SIZE_STRING(s) STACK_SIZE_STRINGX(s)

#ifndef SYSTEM_DRIVER

// The stack used by the driver initialisation thread is not locked; the initialisation
// code will only be called once, and the stack can be treated as free memory, once the
// initialisation routine has returned.
asm ( ".section .init"
    "\n.global _start"
    "\n.type _start, %function"
    "\n_start:"
    "\n\tadr  x17, system"
    "\n\tstr  x0, [x17]"

    "\n\tadr  x16, stack"
    "\n\tadd sp, x16, #8*"STACK_SIZE_STRING( STACK_SIZE )
    "\n\tbl entry"
    "\n\tsvc " ENSTRING( ISAMBARD_RETURN )

    "\n.previous" );
#endif

SYSTEM system = { .r = 0 }; // Initialised by _start code

integer_register __attribute__(( aligned( 16 ) )) stack[STACK_SIZE];

const uint32_t badly_written_driver_exception = 0xbadc0de;

#define GLOBAL_FUNCTION( name ) "\n.global " #name "\n.type " #name ", function\n" #name ":"

#define SYSTEM_CALL( name, code )  \
asm ( ".section .text" \
    GLOBAL_FUNCTION( name ) \
    "\n\tstp x29, x30, [sp, #-16]!" \
    "\n\tsvc " ENSTRING( code ) \
    "\n\tldp x29, x30, [sp], #16" \
    "\n\tret" \
    "\n.previous" );

SYSTEM_CALL( gate_function, ISAMBARD_GATE );
SYSTEM_CALL( interface_to_return, ISAMBARD_INTERFACE_TO_RETURN );
SYSTEM_CALL( interface_to_pass_to, ISAMBARD_INTERFACE_TO_PASS );
SYSTEM_CALL( duplicate_to_pass_to, ISAMBARD_DUPLICATE_TO_PASS );
SYSTEM_CALL( duplicate_to_return, ISAMBARD_DUPLICATE_TO_RETURN );
SYSTEM_CALL( yield, ISAMBARD_YIELD );

asm ( ".section .text"
    GLOBAL_FUNCTION( Isambard_00 )
    GLOBAL_FUNCTION( Isambard_10 )
    GLOBAL_FUNCTION( Isambard_20 )
    GLOBAL_FUNCTION( Isambard_30 )
    GLOBAL_FUNCTION( Isambard_40 )
    GLOBAL_FUNCTION( Isambard_01 )
    GLOBAL_FUNCTION( Isambard_11 )
    GLOBAL_FUNCTION( Isambard_21 )
    GLOBAL_FUNCTION( Isambard_31 )
    GLOBAL_FUNCTION( Isambard_41 )
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc " ENSTRING( ISAMBARD_CALL )
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\n.previous" );

integer_register unknown_call( integer_register call )
{
  for (;;) { asm volatile( "mov x15, %0\n\twfi" : : "r" (call) ); }
  return 0;
}

asm ( ".section .text"
    GLOBAL_FUNCTION( switch_to_partner )
    "\n\tstp x29, x30, [sp, #-32]!"
    "\n\tstp x0, x1, [sp, #16]"
    "\n\tsvc " ENSTRING( ISAMBARD_SWITCH_TO_PARTNER )
    "\n\tldp x16, x17, [sp, #16]"
    "\n\tblr x16"
    "\n\tldp x29, x30, [sp], #32"
    "\n\tret"
    "\n.previous" );

asm ( ".section .text"
    GLOBAL_FUNCTION( get_partner_register )
    "\n\tstp x29, x30, [sp, #-32]!"
    "\n\tsvc " ENSTRING( ISAMBARD_GET_PARTNER_REG )
    "\n\tldp x29, x30, [sp], #32"
    "\n\tret"
    "\n.previous" );

asm ( ".section .text"
    GLOBAL_FUNCTION( set_partner_register )
    "\n\tstp x29, x30, [sp, #-32]!"
    "\n\tsvc " ENSTRING( ISAMBARD_SET_PARTNER_REG )
    "\n\tldp x29, x30, [sp], #32"
    "\n\tret"
    "\n.previous" );

