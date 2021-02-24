#define ISAMBARD_DMA_SOURCE__SERVER( c ) \
static NUMBER c##__DMA_SOURCE__width( c o, unsigned call ); \
static void c##__DMA_SOURCE__send_to( c o, unsigned call, DMA_LOCKED_MEMORY destination ); \
REGISTER c##_call_handler( c o, unsigned call, REGISTER p1, REGISTER p2, REGISTER p3 ) \
{\
  switch (call) {\
  case 0x5ed9f30b: return c##__DMA_SOURCE__width( o, call ); return 0; \
  case 0x17033c7a: c##__DMA_SOURCE__send_to( o, call, DMA_LOCKED_MEMORY_from_REGISTER( p1 ) ).r; \
  }\
}

