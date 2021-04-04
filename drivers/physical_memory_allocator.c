/* Copyright (c) 2020 Simon Willcocks */

#define STACK_SIZE 128

#include "drivers.h"

// The Memory Manager driver is the only code in the system that can access all memory.
// It runs in 1 4k page of code, plus 1 4k page of data.
// All memory is accessible to it with virtual matching physical address, uncached
// It must never touch memory outside the range it is given at startup
// The only map that can make requests of this driver is the system map
// The system map shall ensure that only one core makes a request at a time

#include <aligned_blocks.h>

#define numberof( a ) (sizeof( a ) / sizeof( a[0] ))

typedef struct chunk {
  uint64_t log2size;
  struct chunk *next;
} chunk;

static chunk * freed_chunks = 0;
static chunk * available_chunks = 0;

static const unsigned smallest = 12;
static const unsigned largest = 34;

void inter_map_exception( const char *string )
{
  for (;;) { asm ( "svc 4\n\tsvc 2" : : "r" (string) ); }
}

void release_free_chunk( chunk *p )
{
  chunk **list = &available_chunks;
  chunk *prev = 0;

  while (*list != 0 && (*list) < p) {
    prev = *list;
    list = &prev->next;
  }

  if (*list == p) {
    inter_map_exception( "Corrupt physical memory structure, double free?" );
  }

  if ((char *)p - (char*) prev == (1 << prev->log2size)           // p starts after prev
   && prev->log2size == p->log2size                               // same size
   && ((((integer_register)prev) & (1u << (1 + p->log2size))) == 0)) {    // alignment matches
    // FIXME:
    // Merge chunks if prev + (1 << prev->log2size) == p
    // AND if prev is on an even boundary (i.e. the combined chunk would be
    // on a valid boundary - e.g. combine chunks 2 & 3, not 3 & 4)
  }

  p->next = *list;
  *list = p;
}

void release_first_free_chunk()
{
  chunk *p = freed_chunks;
  if (p != 0) {
    freed_chunks = p->next;
    release_free_chunk( p );
  }
}

void make_list_of_freed_chunks( uint64_t p, uint64_t end )
{
  // Put all the chunks of memory into the freed_chunks list
  // The highest memory chunk will be at the head of the list, so when they're sorted into their chunk-sized lists,
  // there won't need to be any scanning through the list.

  // This occurs before anything can request memory

#define assert( x ) if (!(x)) { asm ( "brk 1" ); }
#define initialise_block( min, max, next_action ) \
        blocks( min, max, \
        { \
          chunk *c = (void*)p; \
          c->next = freed_chunks; \
          c->log2size = min; \
          freed_chunks = c; \
          p += (1ull<<min); }, \
        next_action );

  initialise_block( 12, 13,
  initialise_block( 13, 14,
  initialise_block( 14, 15,
  initialise_block( 15, 16,
  initialise_block( 16, 17,
  initialise_block( 17, 18,
  initialise_block( 18, 19,
  initialise_block( 19, 20,
  initialise_block( 20, 21,
  initialise_block( 21, 22,
  initialise_block( 22, 23,
  initialise_block( 23, 24,
  initialise_block( 24, 25,
  initialise_block( 25, 26,
  initialise_block( 26, 27,
  initialise_block( 27, 28,
  initialise_block( 28, 29,
  initialise_block( 29, 30,
  initialise_block( 30, 31,
  initialise_block( 31, 32,
  initialise_block( 32, 33,
  initialise_block( 33, 34,
  initialise_block( 34, 0,
  )))))))))))))))))))))) );
}

void make_available_all_freed_chunks()
{
  while (freed_chunks) {
    release_first_free_chunk();
  }
}

integer_register allocate( uint32_t size )
{
  integer_register result = 0;

  // FIXME: can probably be made a tiny bit more intelligent!

  chunk **ref = &available_chunks;
  chunk *p = *ref;
  while (p != 0 && (1u << p->log2size) < size) {
    ref = &p->next;
    p = p->next;
  }
  if (p != 0) {
    uint8_t *c = (uint8_t*) p;
    result = c - (uint8_t*) 0;

    // Big enough chunk found. Allocate from the top of it.
    if ((1u << p->log2size) == size) {
      *ref = p->next;
    }
    else {
      // There's still at least one free chunk should stay in the queue
      // *ref will point to the first of them automatically, but the remainder has to be split up

      integer_register remainder = (1u << p->log2size) - size;
      result = result + remainder;

      for (int log2size = p->log2size-1; remainder != 0 && log2size >= 12; log2size--) {
        if (remainder >= (1u << log2size)) {
          p->log2size = log2size;
          remainder -= (1u << log2size);
          if (remainder != 0) {
            chunk *new_chunk = (chunk*) (c + (1u << log2size));
            new_chunk->next = p->next;
            p->next = new_chunk;
            p = new_chunk;
          }
        }
      }
    }
  }

  return result;
}

integer_register entry( uint64_t zero, uint64_t call, uint64_t p1, uint64_t p2 )
{
  zero = zero; // There is only one "object" in this driver
  switch (call) {
  case 0: // Free area
    make_list_of_freed_chunks( p1, p2 );
    make_available_all_freed_chunks();
    return 0;
  case 1: // Allocate block
    return allocate( (p1 + 4095) & ~0xfff );
  }
  asm ( "brk 1" );
  return 0;
}

