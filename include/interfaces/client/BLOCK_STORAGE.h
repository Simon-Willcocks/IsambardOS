#ifndef BLOCK_STORAGE_DEFINED
#define BLOCK_STORAGE_DEFINED
typedef struct { integer_register r; } BLOCK_STORAGE;
#endif
#ifndef BLOCK_STORAGE_DEFINED
#define BLOCK_STORAGE_DEFINED
typedef struct { integer_register r; } BLOCK_STORAGE;
#endif
#ifndef WRITABLE_PHYSICAL_MEMORY_DEFINED
#define WRITABLE_PHYSICAL_MEMORY_DEFINED
typedef struct { integer_register r; } WRITABLE_PHYSICAL_MEMORY;
#endif
static inline NUMBER BLOCK_STORAGE__block_size( BLOCK_STORAGE o )
{
  return Isambard_01( o.r, 0xf615a816 );
}

static inline void BLOCK_STORAGE__read_block( BLOCK_STORAGE o, NUMBER index, WRITABLE_PHYSICAL_MEMORY destination )
{
  Isambard_20( o.r, 0x62351c3e, index.r, destination.r );
}

