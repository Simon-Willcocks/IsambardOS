/* Copyright (c) 2020 Simon Willcocks */

#define STACK_SIZE 128

#include "drivers.h"

asm ( ".section .init"
    "\n.global _start"
    "\n.type _start, %function"
    "\n_start:"
    "\n\tadr  x9, system" // From libdriver.c
    "\n\tstr  x0, [x9]"
    "\n\tadr  x10, stack"
    "\n\tadd sp, x10, #8*"STACK_SIZE_STRING( STACK_SIZE )
    "\n\tbl entry"
    "\n\tsvc 0xfffd"
    "\n.previous" );

unsigned long long stack_lock = 0;
unsigned long long __attribute__(( aligned( 16 ) )) stack[STACK_SIZE] = { 0x33333333 }; // Just a marker

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

static chunk * chunks[34] = { 0 };

static chunk * free_chunks = 0;

static const unsigned smallest = 12;

void inter_map_exception( const char *string )
{
  for (;;) { asm ( "svc 4\n\tsvc 2" : : "r" (string) ); }
}

void release_free_chunk( chunk *p )
{
  unsigned list_index = p->log2size;

  if (list_index < smallest || list_index > numberof( chunks )) {
    inter_map_exception( "Corrupt physical memory structure" );
  }

  chunk **list = &chunks[list_index];
  chunk *prev = 0;

  while (*list != 0 && (*list) < p) {
    prev = *list;
    list = &(*list)->next;
  }

  if (*list == p) {
    inter_map_exception( "Corrupt physical memory structure, double free?" );
  }

  // FIXME:
  // Merge chunks if prev + (1 << prev->log2size) == p

  p->next = *list;
  *list = p;
}

void release_first_free_chunk()
{
  chunk *p = free_chunks;
  if (p != 0) {
    free_chunks = p->next;
    release_free_chunk( p );
  }
}

void make_list_of_free_chunks( uint64_t p, uint64_t end )
{
  // Put all the chunks of memory into the free_chunks list
  // The highest memory chunk will be at the head of the list, so when they're sorted into their chunk-sized lists,
  // there won't need to be any scanning through the list.

  // This occurs before anything can request memory

#define assert( x ) if (!(x)) {}
#define initialise_block( min, max, next_action ) \
        blocks( min, max, \
        { \
          chunk *c = (void*)p; \
          c->next = free_chunks; \
          c->log2size = min; \
          free_chunks = c; \
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

void make_available_all_free_chunks()
{
  while (free_chunks) {
    release_first_free_chunk();
  }
}

void entry( uint64_t zero, uint64_t call, uint64_t start, uint64_t end )
{
  zero = zero; // There is only one "object" in this driver
  switch (call) {
  case 0: // Free area
    make_list_of_free_chunks( start, end );
    make_available_all_free_chunks();
    break;
  }
}

