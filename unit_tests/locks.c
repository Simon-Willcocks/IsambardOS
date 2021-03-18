#include <stdio.h>
#include <inttypes.h>

typedef uint32_t bool;
enum { false = 0, true };

static const uint32_t system_map_index = 1;

void BSOD( int n )
{
  printf( "Failure %d\n", n );
}

typedef uint64_t integer_register;

typedef struct Core Core;
typedef struct thread_context thread_context;

struct thread_context {
  integer_register regs[32-5];
  integer_register pc;
  uint32_t current_map;
  int32_t gate;
  thread_context *next;
  thread_context *prev;
};

struct Core {
  thread_context *runnable;
};

typedef struct {
  thread_context *now;
  thread_context *then;
} thread_switch;

void insert_new_thread_after_old( thread_context *new, thread_context *old )
{
  new->next = old->next;
  new->next->prev = new;
  new->prev = old;
  old->next = new;
}

void insert_thread_at_tail( thread_context **list, thread_context *new )
{
  thread_context *old_head = *list;
  insert_new_thread_after_old( new, old_head->prev );
}

void insert_thread_as_head( thread_context **list, thread_context *new )
{
  if (*list != 0)
    insert_thread_at_tail( list, new );
  else
    new->prev = new->next = new;
  *list = new;
}

void remove_thread( thread_context *thread )
{
  thread->next->prev = thread->prev;
  thread->prev->next = thread->next;
  thread->prev = thread->next = 0;
}

bool is_real_thread( uint64_t t ) { return true; }

void invalidate_all_caches() {}

bool address_is_user_writable( uint64_t a )
{
  return true;
}

uint64_t LDXR( uint64_t p )
{
  return *(uint64_t*)p;
}

bool STXR( uint64_t p, uint64_t v )
{
  *(uint64_t*)p = v;
  return false;
}

void CLREX()
{
}

void dsb()
{
}

#define WITHOUT_SVC
#define WITHOUT_GATE
#define DEBUG_ASM
#define WITHOUT_INTERFACE_CREATION

// Start with the first thread in the runnable queue
thread_context __attribute__(( aligned( 256 ) )) threads[6] = { { .next = threads, .prev = threads, .current_map = system_map_index } };

uint64_t thread_code( thread_context *t ) { return 0x42000000 | (t - threads); }
thread_context *thread_from_code( uint64_t t ) { if (t == 0) return 0; if (0x42000000 != (t & 0xff000000)) BSOD( __COUNTER__); return &threads[t & 0xf]; }

#include "svc_handling.h"

Core the_core = { .runnable = threads };

static inline char id( thread_context *t )
{
  if (t == 0) return '-';
  return 'A' + (t-threads);
}

uint64_t locks[3] = { 0 };

void show()
{
  printf( "Running: " );
  thread_context *t = the_core.runnable;
  do {
    printf( "%c ", id( t ) );
    t = t->next;
  } while (t != the_core.runnable);

  for (int i = 0; i < 3; i++) {
    t = (void*) thread_from_code( locks[i] & 0xffffffff );
    printf( "\tBlocked %d (%c): ", i, id( t ) );
    t = (void*) thread_from_code( (locks[i] >> 32) & 0xffffffff );
    if (t == 0) {
      printf( "-" );
    }
    else {
      thread_context *start = t;
      do {
        if (t->next->prev != t || t->prev->next != t)
          printf( "!" );
        printf( "%c (%ld) ", id( t ), ((uint64_t*) t->regs[17]) - locks );
        t = (void*) t->next;
      } while (t != start);
    }
  }

  printf( "\n" );
}

void Yield()
{
  printf( "Yield: \t\t" );
  the_core.runnable = the_core.runnable->next;
  show();
}

void Lock( int n )
{
  printf( "Lock: (%d)\t", n );
  the_core.runnable->regs[17] = (uint64_t) &locks[n];
  the_core.runnable = handle_svc_wait_for_lock( &the_core, the_core.runnable ).now;
  show();
}

void Release( int n )
{
  printf( "Release: (%d)\t", n );
  the_core.runnable->regs[17] = (uint64_t) &locks[n];
  the_core.runnable = handle_svc_release_lock( &the_core, the_core.runnable ).now;
  show();
}

int main()
{
  threads[0].regs[18] = thread_code( &threads[0] );
  for (int i = 1; i < 6; i++) {
    threads[i].regs[18] = thread_code( &threads[i] );
    insert_new_thread_after_old( &threads[i], &threads[i-1] );
  }

  printf( "%p\n", locks );

  thread_context *owner = 0;

  owner = the_core.runnable;
  Lock( 0 );
  Yield();
  Lock( 0 );
  Lock( 0 );
  the_core.runnable = owner;
  Release( 0 );
  owner = the_core.runnable;
  Yield();
  Lock( 0 );
  the_core.runnable = owner;
  Release( 0 );
  owner = the_core.runnable;
  Release( 0 );
  owner = the_core.runnable;
  Release( 0 );

  return 0;
}
