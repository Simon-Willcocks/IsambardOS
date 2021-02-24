#ifndef DRIVER_SYSTEM_DEFINED
#define DRIVER_SYSTEM_DEFINED
typedef struct { integer_register r; } DRIVER_SYSTEM;
#endif
#ifndef DRIVER_SYSTEM_DEFINED
#define DRIVER_SYSTEM_DEFINED
typedef struct { integer_register r; } DRIVER_SYSTEM;
#endif
#ifndef PHYSICAL_MEMORY_BLOCK_DEFINED
#define PHYSICAL_MEMORY_BLOCK_DEFINED
typedef struct { integer_register r; } PHYSICAL_MEMORY_BLOCK;
#endif
#ifndef PHYSICAL_MEMORY_BLOCK_DEFINED
#define PHYSICAL_MEMORY_BLOCK_DEFINED
typedef struct { integer_register r; } PHYSICAL_MEMORY_BLOCK;
#endif
#ifndef PHYSICAL_MEMORY_BLOCK_DEFINED
#define PHYSICAL_MEMORY_BLOCK_DEFINED
typedef struct { integer_register r; } PHYSICAL_MEMORY_BLOCK;
#endif
#ifndef SERVICE_DEFINED
#define SERVICE_DEFINED
typedef struct { integer_register r; } SERVICE;
#endif
#ifndef INTERRUPT_HANDLER_DEFINED
#define INTERRUPT_HANDLER_DEFINED
typedef struct { integer_register r; } INTERRUPT_HANDLER;
#endif
#ifndef INTERRUPT_HANDLER_DEFINED
#define INTERRUPT_HANDLER_DEFINED
typedef struct { integer_register r; } INTERRUPT_HANDLER;
#endif
static inline PHYSICAL_MEMORY_BLOCK DRIVER_SYSTEM__get_device_page( DRIVER_SYSTEM o, NUMBER physical_address )
{
  return Isambard_11( o.r, 0x10e65c36, physical_address.r );
}

static inline PHYSICAL_MEMORY_BLOCK DRIVER_SYSTEM__get_physical_memory_block( DRIVER_SYSTEM o, NUMBER start, NUMBER size )
{
  return Isambard_21( o.r, 0xb2e624ca, start.r, size.r );
}

static inline void DRIVER_SYSTEM__map_at( DRIVER_SYSTEM o, PHYSICAL_MEMORY_BLOCK block, NUMBER start )
{
  Isambard_20( o.r, 0xbaf19077, block.r, start.r );
}

static inline NUMBER DRIVER_SYSTEM__create_thread( DRIVER_SYSTEM o, NUMBER code, NUMBER stack_top )
{
  return Isambard_21( o.r, 0xbc17ddc4, code.r, stack_top.r );
}

static inline NUMBER DRIVER_SYSTEM__physical_address_of( DRIVER_SYSTEM o, NUMBER va )
{
  return Isambard_11( o.r, 0x4a274f85, va.r );
}

static inline void DRIVER_SYSTEM__register_service( DRIVER_SYSTEM o, NUMBER name_crc, NUMBER provider )
{
  Isambard_20( o.r, 0x3f214cf5, name_crc.r, provider.r );
}

static inline SERVICE DRIVER_SYSTEM__get_service( DRIVER_SYSTEM o, NUMBER name_crc )
{
  return Isambard_11( o.r, 0xc8d4a50f, name_crc.r );
}

static inline NUMBER DRIVER_SYSTEM__get_core_interrupts_count( DRIVER_SYSTEM o )
{
  return Isambard_01( o.r, 0x376b59e9 );
}

static inline NUMBER DRIVER_SYSTEM__get_ms_timer_ticks( DRIVER_SYSTEM o )
{
  return Isambard_01( o.r, 0x9ae123a7 );
}

static inline NUMBER DRIVER_SYSTEM__get_core_timer_value( DRIVER_SYSTEM o )
{
  return Isambard_01( o.r, 0xac18c54c );
}

static inline void DRIVER_SYSTEM__register_interrupt_handler( DRIVER_SYSTEM o, INTERRUPT_HANDLER handler, NUMBER interrupt )
{
  Isambard_20( o.r, 0x5a2dad5d, handler.r, interrupt.r );
}

static inline void DRIVER_SYSTEM__remove_interrupt_handler( DRIVER_SYSTEM o, INTERRUPT_HANDLER handler, NUMBER interrupt )
{
  Isambard_20( o.r, 0x0aa65894, handler.r, interrupt.r );
}

