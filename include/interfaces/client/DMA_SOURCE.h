#ifndef DMA_SOURCE_DEFINED
#define DMA_SOURCE_DEFINED
typedef struct { integer_register r; } DMA_SOURCE;
#endif
#ifndef DMA_SOURCE_DEFINED
#define DMA_SOURCE_DEFINED
typedef struct { integer_register r; } DMA_SOURCE;
#endif
#ifndef DMA_LOCKED_MEMORY_DEFINED
#define DMA_LOCKED_MEMORY_DEFINED
typedef struct { integer_register r; } DMA_LOCKED_MEMORY;
#endif
static inline NUMBER DMA_SOURCE__width( DMA_SOURCE o )
{
  return Isambard_01( o.r, 0x5ed9f30b );
}

static inline void DMA_SOURCE__send_to( DMA_SOURCE o, DMA_LOCKED_MEMORY destination )
{
  Isambard_10( o.r, 0x17033c7a, destination.r );
}

