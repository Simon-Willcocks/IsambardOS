#ifndef DMA_LOCKED_MEMORY_DEFINED
#define DMA_LOCKED_MEMORY_DEFINED
typedef struct { integer_register r; } DMA_LOCKED_MEMORY;
#endif
#ifndef DMA_LOCKED_MEMORY_DEFINED
#define DMA_LOCKED_MEMORY_DEFINED
typedef struct { integer_register r; } DMA_LOCKED_MEMORY;
#endif
#ifndef DMA_LOCKED_MEMORY_DEFINED
#define DMA_LOCKED_MEMORY_DEFINED
typedef struct { integer_register r; } DMA_LOCKED_MEMORY;
#endif
static inline void DMA_LOCKED_MEMORY__copy_to_memory( DMA_LOCKED_MEMORY o, DMA_LOCKED_MEMORY destination )
{
  Isambard_10( o.r, 0x8a8c15fa, destination.r );
}

