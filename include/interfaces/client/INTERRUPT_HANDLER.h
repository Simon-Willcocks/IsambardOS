#ifndef INTERRUPT_HANDLER_DEFINED
#define INTERRUPT_HANDLER_DEFINED
typedef struct { integer_register r; } INTERRUPT_HANDLER;
#endif
#ifndef INTERRUPT_HANDLER_DEFINED
#define INTERRUPT_HANDLER_DEFINED
typedef struct { integer_register r; } INTERRUPT_HANDLER;
#endif
static inline void INTERRUPT_HANDLER__interrupt( INTERRUPT_HANDLER o )
{
  Isambard_00( o.r, 0x78bdc371 );
}

