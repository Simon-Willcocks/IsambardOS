#define ISAMBARD_DMA_LOCKED_MEMORY__SERVER( c ) \
static void c##__DMA_LOCKED_MEMORY__copy_to_memory( c o, unsigned call, DMA_LOCKED_MEMORY destination ); \
REGISTER c##_call_handler( c o, unsigned call, REGISTER p1, REGISTER p2, REGISTER p3 ) \
{\
  switch (call) {\
  case 0x8a8c15fa: c##__DMA_LOCKED_MEMORY__copy_to_memory( o, call, DMA_LOCKED_MEMORY_from_REGISTER( p1 ) ).r; \
  }\
}

