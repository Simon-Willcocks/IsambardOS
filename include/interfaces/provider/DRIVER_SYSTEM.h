#define ISAMBARD_DRIVER_SYSTEM__SERVER( c ) \
static PHYSICAL_MEMORY_BLOCK c##__DRIVER_SYSTEM__get_device_page( c o, unsigned call, NUMBER physical_address ); \
static PHYSICAL_MEMORY_BLOCK c##__DRIVER_SYSTEM__get_physical_memory_block( c o, unsigned call, NUMBER start, NUMBER size ); \
static void c##__DRIVER_SYSTEM__map_at( c o, unsigned call, PHYSICAL_MEMORY_BLOCK block, NUMBER start ); \
static NUMBER c##__DRIVER_SYSTEM__create_thread( c o, unsigned call, NUMBER code, NUMBER stack_top ); \
static NUMBER c##__DRIVER_SYSTEM__physical_address_of( c o, unsigned call, NUMBER va ); \
static void c##__DRIVER_SYSTEM__register_service( c o, unsigned call, NUMBER name_crc, NUMBER provider ); \
static SERVICE c##__DRIVER_SYSTEM__get_service( c o, unsigned call, NUMBER name_crc ); \
static NUMBER c##__DRIVER_SYSTEM__get_core_interrupts_count( c o, unsigned call ); \
static NUMBER c##__DRIVER_SYSTEM__get_ms_timer_ticks( c o, unsigned call ); \
static NUMBER c##__DRIVER_SYSTEM__get_core_timer_value( c o, unsigned call ); \
static void c##__DRIVER_SYSTEM__register_interrupt_handler( c o, unsigned call, INTERRUPT_HANDLER handler, NUMBER interrupt ); \
static void c##__DRIVER_SYSTEM__remove_interrupt_handler( c o, unsigned call, INTERRUPT_HANDLER handler, NUMBER interrupt ); \
REGISTER c##_call_handler( c o, unsigned call, REGISTER p1, REGISTER p2, REGISTER p3 ) \
{\
  switch (call) {\
  case 0x10e65c36: return c##__DRIVER_SYSTEM__get_device_page( o, call, NUMBER_from_REGISTER( p1 ) ); return 0; \
  case 0xb2e624ca: return c##__DRIVER_SYSTEM__get_physical_memory_block( o, call, NUMBER_from_REGISTER( p1 ), NUMBER_from_REGISTER( p2 ) ); return 0; \
  case 0xbaf19077: c##__DRIVER_SYSTEM__map_at( o, call, PHYSICAL_MEMORY_BLOCK_from_REGISTER( p1 ), NUMBER_from_REGISTER( p2 ) ).r; \
  case 0xbc17ddc4: return c##__DRIVER_SYSTEM__create_thread( o, call, NUMBER_from_REGISTER( p1 ), NUMBER_from_REGISTER( p2 ) ); return 0; \
  case 0x4a274f85: return c##__DRIVER_SYSTEM__physical_address_of( o, call, NUMBER_from_REGISTER( p1 ) ); return 0; \
  case 0x3f214cf5: c##__DRIVER_SYSTEM__register_service( o, call, NUMBER_from_REGISTER( p1 ), NUMBER_from_REGISTER( p2 ) ).r; \
  case 0xc8d4a50f: return c##__DRIVER_SYSTEM__get_service( o, call, NUMBER_from_REGISTER( p1 ) ); return 0; \
  case 0x376b59e9: return c##__DRIVER_SYSTEM__get_core_interrupts_count( o, call ); return 0; \
  case 0x9ae123a7: return c##__DRIVER_SYSTEM__get_ms_timer_ticks( o, call ); return 0; \
  case 0xac18c54c: return c##__DRIVER_SYSTEM__get_core_timer_value( o, call ); return 0; \
  case 0x5a2dad5d: c##__DRIVER_SYSTEM__register_interrupt_handler( o, call, INTERRUPT_HANDLER_from_REGISTER( p1 ), NUMBER_from_REGISTER( p2 ) ).r; \
  case 0x0aa65894: c##__DRIVER_SYSTEM__remove_interrupt_handler( o, call, INTERRUPT_HANDLER_from_REGISTER( p1 ), NUMBER_from_REGISTER( p2 ) ).r; \
  }\
}

