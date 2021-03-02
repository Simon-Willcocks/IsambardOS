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

asm ( ".section .text"
    GLOBAL_FUNCTION( inter_map_call_0p )
    GLOBAL_FUNCTION( inter_map_call_1p )
    GLOBAL_FUNCTION( inter_map_call_2p )
    GLOBAL_FUNCTION( inter_map_call_3p )
    GLOBAL_FUNCTION( inter_map_procedure_0p )
    GLOBAL_FUNCTION( inter_map_procedure_1p )
    GLOBAL_FUNCTION( inter_map_procedure_2p )
    GLOBAL_FUNCTION( inter_map_procedure_3p )
    GLOBAL_FUNCTION( inter_map_call_returning_object_0p )
    GLOBAL_FUNCTION( inter_map_call_returning_object_1p )
    GLOBAL_FUNCTION( inter_map_call_returning_object_2p )
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
