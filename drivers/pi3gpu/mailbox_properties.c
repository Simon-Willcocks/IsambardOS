/* Copyright (c) 2021 Simon Willcocks */

#include "devices.h"
#include "exclusive.h"

#define numberof( a ) (sizeof( a ) / sizeof( a[0] ))

ISAMBARD_INTERFACE( GPU_MAILBOX_MANAGER )
ISAMBARD_INTERFACE( GPU_MAILBOX )
ISAMBARD_INTERFACE( GPU_MAILBOX_CLIENT )

#include "interfaces/client/GPU_MAILBOX.h"
#include "interfaces/provider/GPU_MAILBOX.h"

ISAMBARD_GPU_MAILBOX_CLIENT__SERVER( GPU_MAILBOX )
ISAMBARD_PROVIDER( GPU_MAILBOX, AS_GPU_MAILBOX_CLIENT( GPU_MAILBOX ) )

GPU_MAILBOX mailbox = {};
ISAMBARD_STACK( mbc_stack, 32 );
ISAMBARD_PROVIDER_NO_LOCK_AND_SINGLE_STACK( GPU_MAILBOX, RETURN_FUNCTIONS_GPU_MAILBOX_CLIENT( GPU_MAILBOX ), mbc_stack, 32 * 8 )

static const NUMBER tag_channel = { .r = 8 };

static struct {
  uint32_t pa;
  uint32_t requesting_thread;
} __attribute__(( aligned( 16 ) )) outstanding_requests[64] = { { 0, 0 } };

// outstanding_requests forms a queue. The requests_head is modified by the queueing code,
// requests_tail is modified by the receiving code. If both values are equal, the queue is empty.
// The individual entries are set from zero to non-zero by the queuing code, from non-zero to zero
// by the receiving code.
unsigned volatile requests_head = 0;
unsigned volatile requests_tail = 0;

// If a thread, holding the mailbox_tag_request lock, finds the queue full
// (next_outstanding_request( requests_head ) == requests_tail)
// it sets this variable, and waits to be woken.
static volatile uint32_t blocked_thread = 0;

// This lock is to avoid the possibility that an interrupt will occur between checking the queue
// is full and setting the blocked_thread variable. Said interrupt would also have to handle all
// numberof( outstanding_requests ) requests before returning the next instruction, to really mess
// things up, but better safe than sorry!
static uint64_t outstanding_requests_lock = 0;


static inline unsigned next_outstanding_request( unsigned n )
{
  return (n + 1) % numberof( outstanding_requests );
}

void GPU_MAILBOX__GPU_MAILBOX_CLIENT__incoming_message( GPU_MAILBOX mbox, NUMBER message )
{
  mbox = mbox;

  for (unsigned i = requests_tail; i != requests_head; i = next_outstanding_request( i )) {
    if (outstanding_requests[i].requesting_thread != 0
     && outstanding_requests[i].pa == message.r) {
      uint32_t thread = outstanding_requests[i].requesting_thread;
      wake_thread( thread );
      // Removed one entry from the list, move the tail closer to the head
      if (i != requests_tail) {
        outstanding_requests[i] = outstanding_requests[requests_tail];
      }
      outstanding_requests[requests_tail].pa = 0;
      outstanding_requests[requests_tail].requesting_thread = 0;

      claim_lock( &outstanding_requests_lock );

      requests_tail = next_outstanding_request( requests_tail );

      release_lock( &outstanding_requests_lock );

      if (blocked_thread != 0) {
        thread = blocked_thread;
        blocked_thread = 0;
        wake_thread( thread );
      }

      GPU_MAILBOX__GPU_MAILBOX_CLIENT__incoming_message__return();
    }
  }

asm ( "mov x27, %[r]\n\tbrk 5" :: [r] "r" (message.r) );
  GPU_MAILBOX__exception( message.r );
}

void flush_and_invalidate_cache( void *start, int length )
{
  dsb();

  static uint64_t cache_line_size = 3; // Probably higher, but non-zero ensures loop completes
  if (cache_line_size == 3) {
    asm volatile ( "mrs %[s], DCZID_EL0" : [s] "=r" (cache_line_size) );
  }

  integer_register p = (integer_register) start;
  length += (p & ((1 << cache_line_size)-1));
  p = p & ~((1 << cache_line_size)-1);
  while (length > 0) {
    asm volatile ( "dc civac, %[va]" : : [va] "r" (p) );
    p += (1 << cache_line_size);
    length -= (1 << cache_line_size);
  }
}

// Allows for multiple threads, one request per thread at a time.
void mailbox_tag_request( uint32_t *va )
{
  static uint64_t lock = 0;

  NUMBER pa = DRIVER_SYSTEM__physical_address_of( driver_system(),
                NUMBER__from_pointer( (void*) va ) );

  flush_and_invalidate_cache( va, va[0] );

  claim_lock( &lock );

  if (mailbox.r == 0) {
    GPU_MAILBOX_MANAGER mailboxes = GPU_MAILBOX_MANAGER__get_service( "Pi GPU Mailboxes", -1 );
    mailbox = GPU_MAILBOX_MANAGER__claim_channel( mailboxes, tag_channel );
    // TODO Free mailboxes

    GPU_MAILBOX_CLIENT client = GPU_MAILBOX__GPU_MAILBOX_CLIENT__to_pass_to( mailbox.r, (void*) mailbox.r );
    GPU_MAILBOX__register_client( mailbox, client ); 
  }

  claim_lock( &outstanding_requests_lock );
  bool list_full = requests_tail == next_outstanding_request( requests_head );

  if (list_full) {
    blocked_thread = this_thread;
  }
  release_lock( &outstanding_requests_lock );
  if (list_full) {
    wait_until_woken(); // blocked_thread
  }

  outstanding_requests[requests_head].pa = pa.r;
  outstanding_requests[requests_head].requesting_thread = this_thread;

  requests_head = next_outstanding_request( requests_head );

  GPU_MAILBOX__queue_message( mailbox, pa );

  release_lock( &lock );

  wait_until_woken(); // outstanding_requests[...].requesting_thread

  flush_and_invalidate_cache( va, va[0] );
}
