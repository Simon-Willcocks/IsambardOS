#define ISAMBARD_BLOCK_STORAGE__SERVER( c ) \
static NUMBER c##__BLOCK_STORAGE__block_size( c o, unsigned call ); \
static void c##__BLOCK_STORAGE__read_block( c o, unsigned call, NUMBER index, WRITABLE_PHYSICAL_MEMORY destination ); \
REGISTER c##_call_handler( c o, unsigned call, REGISTER p1, REGISTER p2, REGISTER p3 ) \
{\
  switch (call) {\
  case 0xf615a816: return c##__BLOCK_STORAGE__block_size( o, call ); return 0; \
  case 0x62351c3e: c##__BLOCK_STORAGE__read_block( o, call, NUMBER_from_REGISTER( p1 ), WRITABLE_PHYSICAL_MEMORY_from_REGISTER( p2 ) ).r; \
  }\
}

