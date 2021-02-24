#ifndef DMA_SINK_DEFINED
#define DMA_SINK_DEFINED
typedef struct { integer_register r; } DMA_SINK;
#endif
#ifndef DMA_SINK_DEFINED
#define DMA_SINK_DEFINED
typedef struct { integer_register r; } DMA_SINK;
#endif
#ifndef DMA_LOCKED_MEMORY_DEFINED
#define DMA_LOCKED_MEMORY_DEFINED
typedef struct { integer_register r; } DMA_LOCKED_MEMORY;
#endif
static inline NUMBER DMA_SINK__width( DMA_SINK o )
{
  return Isambard_01( o.r, 0x5aca30e7 );
}

static inline void DMA_SINK__read_from( DMA_SINK o, DMA_LOCKED_MEMORY source )
{
  Isambard_10( o.r, 0x01b41f51, source.r );
}

