#include "drivers.h"

SYSTEM system = { .r = 0 }; // Initialised by _start code

#define GLOBAL_FUNCTION( name ) "\n.global " #name "\n.type " #name ", function\n" #name ":"

#define SYSTEM_CALL( name, code )  \
asm ( ".section .text" \
    GLOBAL_FUNCTION( name ) \
    "\n\tstp x29, x30, [sp, #-16]!" \
    "\n\tsvc " #code \
    "\n\tldp x29, x30, [sp], #16" \
    "\n\tret" \
    "\n.previous" );

SYSTEM_CALL( object_to_return, 0xfff9 );
SYSTEM_CALL( object_to_pass_to, 0xfff8 );
SYSTEM_CALL( duplicate_to_pass_to, 0xfff7 );
SYSTEM_CALL( duplicate_to_return, 0xfff6 );
SYSTEM_CALL( yield, 0xfffc );

asm (
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
    "\n\tsvc 0xfffe"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\n.previous" );

integer_register unknown_call( integer_register call )
{
  for (;;) { asm volatile( "mov x15, %0\n\twfi" : : "r" (call) ); }
  return 0;
}
