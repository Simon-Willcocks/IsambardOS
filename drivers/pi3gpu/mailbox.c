/* Copyright 2021 Simon Willcocks */

/* Not very general, yet.
 * Allows a channel owner to send a request, and return to them with a response.
 * Does not allow ARM to act as a server.
 * Does not allow multiple requests on a channel at a time.
 * Multiple messages per channel is obviously anticipated: FIFO depth > # channels
 */

#include "devices.h"
#include "exclusive.h"

#define assert( c ) if (!(c)) { asm ( "brk 7" ); }

struct {
  uint32_t waiting_thread;
  uint32_t message;
  enum { idle, waiting_to_send, waiting_to_send_expecting_response, waiting_for_response, response_received } state;
} channel[16] = { { 0 } };

static int last_sender = 0;
static int next_channel( int c )
{
  return (c + 1) % 16;
}

uint32_t debug_mailbox_message = -1;
uint32_t debug_mailbox_state = -1;

void mailbox_interrupt()
{
  uint32_t mailbox0_pending = devices.mailbox[0].config;
  uint32_t mailbox1_pending = devices.mailbox[1].config;

  if ((mailbox0_pending & 0x10) != 0) { // Not empty
    while (0 == (devices.mailbox[0].status & 0x40000000)) {
      uint32_t message = devices.mailbox[0].value;
      if (channel[message & 0xf].state == waiting_for_response) {
        channel[message & 0xf].message = message;
        channel[message & 0xf].state = response_received;
        wake_thread( channel[message & 0xf].waiting_thread );
      }
      else {
        asm ( "brk 8" );
      }
    }
    // Query: does emptying the fifo remove the interrupt?
  }
  memory_read_barrier(); // Completed our reads of devices.mailbox

  if ((mailbox1_pending & 0x20) != 0) { // Not full
    memory_write_barrier(); // About to write to devices.mailbox
    // Send as many unsent messages as possible, rotating through the channels, and
    // waking the waiting threads. If no channels have anything to send afterwards,
    // disable the interrupt.
    assert( 0 == (devices.mailbox[1].status & 0x80000000) );
    int c = last_sender;
    int new_last_sender = last_sender;
    do {
      c = next_channel( c );
      if (channel[c].state == waiting_to_send
       || channel[c].state == waiting_to_send_expecting_response) {
        devices.mailbox[1].value = channel[c].message;
        if (channel[c].state == waiting_to_send_expecting_response) {
          channel[c].state = waiting_for_response;
        }
        else {
          wake_thread( channel[c].waiting_thread );
        }
        new_last_sender = c;
      }
    } while (0 == (devices.mailbox[1].status & 0x80000000) && c != last_sender);

    if (0 == (devices.mailbox[1].status & 0x80000000)) {
      // Mailbox still isn't full, no need to wait
      devices.mailbox[1].config = 0; // No interrups
    }
    last_sender = new_last_sender;
  }
}

#include "interfaces/provider/GPU_MAILBOX.h"
#include "interfaces/provider/SERVICE.h"

typedef NUMBER MBOX;
typedef NUMBER Channel;

ISAMBARD_GPU_MAILBOX__SERVER( MBOX )
ISAMBARD_SERVICE__SERVER( MBOX )
ISAMBARD_PROVIDER( MBOX, AS_GPU_MAILBOX( MBOX ) ; AS_SERVICE( MBOX ) )

ISAMBARD_GPU_MAILBOX_CHANNEL__SERVER( Channel )
ISAMBARD_PROVIDER( Channel, AS_GPU_MAILBOX_CHANNEL( Channel ) )

integer_register mailbox_stack_lock = 0;
integer_register __attribute__(( aligned(16) )) mailbox_stack[64];

// Using the driver initialisation stack
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( MBOX, mailbox_stack_lock, mailbox_stack, 64 * 8 )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( Channel, mailbox_stack_lock, mailbox_stack, 64 * 8 )

void expose_gpu_mailbox()
{
  SERVICE obj = MBOX_SERVICE_to_pass_to( system.r, NUMBER_from_integer_register( 0 ) );
  register_service( "Pi GPU Mailboxes", obj );
}

GPU_MAILBOX_CHANNEL MBOX__GPU_MAILBOX__claim_channel( MBOX o, NUMBER channel )
{
  o = o;

  if (channel.r >= 15) asm ( "brk 7" );

  memory_write_barrier(); // About to write to devices.mailbox
  devices.mailbox[0].config = 1; // Not-empty interrupt

  return Channel_GPU_MAILBOX_CHANNEL_to_return( channel );
}

// Only two entities adjust the config member of the mailboxes,
// this function, and the interrupt handler routine.
// The only interesting interrupt on the sending mailbox is not-full,
// I don't care when it becomes not-empty (because that was me), or
// empty.

static uint64_t send_lock = 0;

static void send_message( uint32_t message, int dest, bool response_expected )
{
  bool wait_to_continue = response_expected;

  claim_lock( &send_lock );

  if (0 == (devices.mailbox[1].status & 0x80000000)) {
    channel[dest].state = response_expected ? waiting_for_response : idle;
    channel[dest].waiting_thread = this_thread;
    memory_write_barrier(); // About to write to devices.mailbox
    devices.mailbox[1].value = message | dest;
  }
  else {
    channel[dest].state = response_expected ? waiting_to_send_expecting_response : waiting_to_send;
    channel[dest].message = message | dest;
    channel[dest].waiting_thread = this_thread;
    memory_write_barrier(); // About to write to devices.mailbox
    devices.mailbox[1].config = 2; // Not-full interrupt

    // If the send is delayed, wait until it's put in the queue by the interrupt handler, or the
    // response is received. Either way, this thread has to pause.
    wait_to_continue = true;
  }

  release_lock( &send_lock );

  if (wait_to_continue) {
    wait_until_woken(); // I might have to wait to send, I *will* be waiting for the response.
  }

  asm ( "svc 0" );
}

NUMBER Channel__GPU_MAILBOX_CHANNEL__send_for_response( Channel c, NUMBER message )
{
  if (c.r > 15) asm ( "brk 8" );
  if (channel[c.r].state != idle) asm ( "brk 8" );
  if ((message.r & 0xf) != 0) asm ( "brk 9" );

  send_message( message.r, c.r, true );

  if (channel[c.r].state != response_received) asm ( "brk 4" );
  if ((channel[c.r].message & 0xf) != c.r) asm ( "brk 5" );

  channel[c.r].state = idle;

  return NUMBER_from_integer_register( channel[c.r].message & ~0xf );
}

#define MAX_REQUEST_SIZE 256 + 6

static uint32_t __attribute__(( aligned( 16 ) )) mailbox_request[MAX_REQUEST_SIZE] = { 0x20202020, 0x30303030, 0x40404040 };
uint32_t *const mailbox_request_buffer = &mailbox_request[5];

// Routines 

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

uint32_t tag_request()
{
  static uint32_t phys_addr = 0;
  if (phys_addr == 0) {
    // Welcome back to the days of himem and lomem in a PC...
    uint64_t address = DRIVER_SYSTEM__physical_address_of( driver_system(), NUMBER_from_pointer( &mailbox_request ) ).r;
    if (address >> 32) {
      asm ( "brk 2" );
    }
    phys_addr = (uint32_t) address;
  }
  return phys_addr;
}

// Read/Write a single tag; buffer_size should be greater of request and response sizes
uint32_t single_mailbox_tag_access( uint32_t tag, uint32_t buffer_size )
{
  if (buffer_size > MAX_REQUEST_SIZE - 6) {
     for (;;) { asm volatile ( "svc 1\n\tsvc 6" ); }
  }

  mailbox_request[0] = buffer_size + 6 * sizeof( mailbox_request[0] );
  mailbox_request[1] = 0;
  mailbox_request[2] = tag;
  mailbox_request[3] = buffer_size;
  mailbox_request[4] = 0; // request
  mailbox_request[5 + (buffer_size / sizeof( mailbox_request[0] ))] = 0; // end tag

  flush_and_invalidate_cache( mailbox_request, mailbox_request[0] );

  send_message( tag_request(), 8, true );

  return mailbox_request[4];
}

