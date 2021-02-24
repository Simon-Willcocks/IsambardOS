#define ISAMBARD_DMA_SINK__SERVER( c ) \
static NUMBER c##__DMA_SINK__width( c o, unsigned call ); \
static void c##__DMA_SINK__read_from( c o, unsigned call, DMA_LOCKED_MEMORY source ); \
REGISTER c##_call_handler( c o, unsigned call, REGISTER p1, REGISTER p2, REGISTER p3 ) \
{\
  switch (call) {\
  case 0x5aca30e7: return c##__DMA_SINK__width( o, call ); return 0; \
  case 0x01b41f51: c##__DMA_SINK__read_from( o, call, DMA_LOCKED_MEMORY_from_REGISTER( p1 ) ).r; \
  }\
}

