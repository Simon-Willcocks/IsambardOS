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
  integer_register regs[32];
  integer_register pc;
  uint32_t current_map;
  uint32_t gate;
  thread_context *next;
  thread_context *prev;
  const char *id; // For debug output
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

int main()
{
  handle_svc_gate( 0, 0, 0 );
  return 0;
}
