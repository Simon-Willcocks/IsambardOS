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
  thread_context *interrupt_thread;
  thread_context *blocked_with_timeout;
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

void remove_thread( thread_context *thread )
{
  thread->next->prev = thread->prev;
  thread->prev->next = thread->next;
  thread->prev = thread->next = 0;
}

uint64_t thread_code( thread_context *t ) { return (uint64_t) t; }
thread_context * thread_from_code( uint64_t t ) { return (void*) t; }

bool is_real_thread( uint64_t t ) { return true; }

void invalidate_all_caches() {}

#define WITHOUT_SVC
#define WITHOUT_LOCKS
#define WITHOUT_
#define WITHOUT_INTERFACE_CREATION
#include "svc_handling.h"

// Start with the first thread in the runnable queue
// For gates, that is also the interrupt thread
thread_context __attribute__(( aligned( 256 ) )) threads[6] = { { .next = threads, .prev = threads, .current_map = system_map_index } };

Core the_core = { .runnable = threads, .interrupt_thread = threads };

thread_context *ithread = threads;

static inline char id( thread_context *t )
{
  return (t - threads) == 0 ? 'i' : '@' + (t-threads);
}

void show()
{
  printf( "Running: " );
  thread_context *t = the_core.runnable;
  do {
    printf( "%c ", id( t ) );
    t = t->next;
  } while (t != the_core.runnable);

  printf( "\tBlocked: " );
  t = the_core.blocked_with_timeout;
  if (t == 0) {
    printf( "-" );
  }
  else {
    uint64_t prev = (uint64_t) &the_core.blocked_with_timeout;
    do {
      if (t->regs[16] != prev)
        printf( "!" );
      printf( "%c (%ld) ", id( t ), t->regs[1] );
      prev = (uint64_t) &t->regs[17];
      t = (void*) t->regs[17];
    } while (t != 0);
  }

  printf( "\n" );
}

void Yield()
{
  printf( "Yield: \t\t" );
  the_core.runnable = the_core.runnable->next;
  show();
}

void WFI()
{
  printf( "WFI: \t\t" );
  the_core.runnable = the_core.runnable->next;
  remove_thread( ithread );
  show();
}

void Tick()
{
  printf( "Tick: \t\t" );
  insert_new_thread_after_old( ithread, the_core.runnable->prev );
  the_core.runnable = ithread;
  ithread->regs[0] = 0;
  ithread->regs[1] = 0;
  handle_svc_gate( &the_core, the_core.runnable );
  show();
}

void Wait( int timeout )
{
  printf( "Wait( %d ): \t", timeout );
  the_core.runnable->regs[0] = 0;
  the_core.runnable->regs[1] = timeout;
  handle_svc_gate( &the_core, the_core.runnable );
  show();
}

void Wake( thread_context *t )
{
  printf( "Wake( %c ):\t", id( t ) );
  the_core.runnable->regs[0] = thread_code( t );
  handle_svc_gate( &the_core, the_core.runnable );
  show();
}

int main()
{
  for (int i = 1; i < 6; i++) {
    insert_new_thread_after_old( &threads[i], &threads[i-1] );
  }
  WFI();

  Tick(); WFI();
  // Simple sleep
  Wait( 3 ); Tick(); WFI(); Tick(); WFI(); Tick(); WFI();

  // Two threads, should wake at the same time
  Wait( 6 ); Tick(); WFI(); Tick(); WFI(); Tick(); WFI();
  Wait( 3 ); Tick(); WFI(); Tick(); WFI(); Tick(); WFI();

  // Ditto, both waiting between two ticks
  Wait( 3 ); Wait( 3 );
  Tick(); WFI(); Tick(); WFI(); Tick(); WFI();

  // First returns, then second
  Wait( 3 ); Wait( 5 );
  Tick(); WFI(); Tick(); WFI(); Tick(); WFI();
  Tick(); WFI(); Tick(); WFI(); Tick(); WFI();

  // Second returns, then first
  Wait( 5 ); Wait( 3 );
  Tick(); WFI(); Tick(); WFI(); Tick(); WFI();
  Tick(); WFI(); Tick(); WFI(); Tick(); WFI();

  Wait( 5 ); Wait( 3 ); Wait( 6 );
  Tick(); WFI(); Tick(); WFI(); Tick(); WFI();
  Tick(); WFI(); Tick(); WFI(); Tick(); WFI();

  thread_context *waiting = the_core.runnable;
  Wait( 0 ); // No timeout
  Wait( 2 );
  Wait( 1 );
  Tick(); WFI();
  Wake( waiting );
  Tick(); WFI();
  Tick(); WFI();
  Tick(); WFI();

  return 0;
}
