/* Copyright 2021 Simon Willcocks */

/* I believe the following to be true:
 * The GPU runs independently of the ARM,
 * The GPU can process at least one message at a time, maybe more than one,
 * The FIFO mailbox has a fixed capacity.
 * The mailbox messages point to buffers by their physical addresses
 * The reponse mailbox will return those buffers through the other mailbox
 * It makes no sense to send a buffer to the GPU a second time, without an intermediate response
 * The buffers don't necessarily have to be returned in the same order they were sent, even in the same channel
 * It is possible that an interface allows the GPU to allocate buffers, and pass them to the ARM.
 * The mailboxes can interrupt the ARM when they are not-empty, and not-full. (At least.)
 *
 *
 * Not very general, yet.
 * Allows a channel owner to send a request, and return to them with a response.
 * Does not allow ARM to act as a server.
 * Does not allow multiple requests on a channel at a time.
 * Multiple messages per channel is obviously anticipated: FIFO depth > # channels
 */

#include "devices.h"
#include "exclusive.h"

ISAMBARD_INTERFACE( GPU_MAILBOX_MANAGER )
ISAMBARD_INTERFACE( GPU_MAILBOX )
ISAMBARD_INTERFACE( GPU_MAILBOX_CLIENT )

#define assert( c ) if (!(c)) { asm ( "brk 7" ); }

typedef struct mailbox_channel mailbox_channel;
typedef struct mailbox_channel *MBOX;

struct mailbox_channel {
  uint64_t lock; // Protects access to waiting_thread, client.
  uint32_t waiting_thread;
  uint32_t received_message;
  GPU_MAILBOX_CLIENT client;
};

struct mailbox_channel channels[16] = {};

static uint32_t blocking_message = 0;
static uint64_t blocked_sending_thread = 0;

extern uint32_t *mapped_memory;

void mailbox_interrupt()
{
  uint32_t mailbox0_pending = devices.mailbox[0].config;

if (mapped_memory != 0) {
  mapped_memory[16] = mailbox0_pending;
  mapped_memory[18] |= mailbox0_pending;
}

  if ((mailbox0_pending & 0x10) != 0) { // Not empty
    if (blocking_message != 0) {
      int channel = blocking_message & 0xf;
      if (channels[channel].received_message == 0) {
        channels[channel].received_message = blocking_message & ~0xf;
        blocking_message = 0;
        wake_thread( channels[channel].waiting_thread );
      }
      else {
        asm ( "brk 4" ); // How?
      }
    }
    while (0 == (devices.mailbox[0].status & 0x40000000)) { // Not empty
      uint32_t message = devices.mailbox[0].value; // Could peek, but using blocking_message is probably faster, and allows the GPU to insert one more message into the mailbox

      int channel = message & 0xf;
      if (channels[channel].waiting_thread == 0) {
        // Message on unclaimed channel, discard
        asm ( "brk 8" );
        continue;
      }
      if (channels[channel].received_message == 0) {
        channels[channel].received_message = message & ~0xf;
        wake_thread( channels[channel].waiting_thread );
      }
      else {
        // Channel client hasn't dealt with previous message yet. Blocked.
        // TODO Keep an eye on how often this happens; maybe implement a fifo for each channel in software.
        blocking_message = message;
        break;
      }
    }
    // Emptying the fifo removes the interrupt, if not every message can be delivered yet, pause the interrupts
    if (0 != blocking_message) {
      memory_write_barrier(); // About to write to devices.mailbox
      devices.mailbox[0].config = 0; // No interrupts, we're blocked
    }
  }
  memory_read_barrier(); // Completed our reads of devices.mailbox
  if (blocked_sending_thread != 0) {
    wake_thread( blocked_sending_thread );
  }
}

#include "interfaces/provider/GPU_MAILBOX.h"
#include "interfaces/client/GPU_MAILBOX.h"

typedef NUMBER MANAGER;

integer_register mailbox_stack_lock = 0;
ISAMBARD_STACK( mailbox_stack, 64 );

ISAMBARD_GPU_MAILBOX_MANAGER__SERVER( MANAGER )
ISAMBARD_PROVIDER( MANAGER, AS_GPU_MAILBOX_MANAGER( MANAGER ) )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( MANAGER, RETURN_FUNCTIONS_GPU_MAILBOX_MANAGER( MANAGER ), mailbox_stack_lock, mailbox_stack, 64 * 8 )

ISAMBARD_GPU_MAILBOX__SERVER( MBOX )
ISAMBARD_PROVIDER( MBOX, AS_GPU_MAILBOX( MBOX ) )

// Using the driver initialisation stack
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( MBOX, RETURN_FUNCTIONS_GPU_MAILBOX( MBOX ), mailbox_stack_lock, mailbox_stack, 64 * 8 )

PHYSICAL_MEMORY_BLOCK test_memory = {};
uint32_t *mapped_memory = 0;

void expose_gpu_mailbox()
{
  test_memory = SYSTEM__allocate_memory( system, NUMBER__from_integer_register( 4096 ) );
  DRIVER_SYSTEM__map_at( driver_system(), test_memory, NUMBER__from_integer_register( 0x10000 ) );
  mapped_memory = (void*) (0x10000);
  for (int i = 0; i < 1024; i++) mapped_memory[i] = 0;

  MANAGER__GPU_MAILBOX_MANAGER__register_service( "Pi GPU Mailboxes", 0 );
}

void MANAGER__GPU_MAILBOX_MANAGER__claim_channel( MANAGER o, NUMBER channel )
{
  o = o;
  if (channel.r > 15) MANAGER__exception( name_code( "Channel number out of range" ).r );
  MANAGER__GPU_MAILBOX_MANAGER__claim_channel__return( MBOX__GPU_MAILBOX__to_return( (void*) &channels[channel.r] ) );
}

// Should we have one of these per channel with a registered client?
void __attribute__(( noreturn )) message_pump()
{
  int last_channel = 0;
  for (;;) {
    int messages = wait_until_woken() + 1;
    for (int j = 0; j < messages; j++) {
      for (int i = 0; i < 17; i++) {
        int c = (last_channel+i) % 16;
        if (channels[c].waiting_thread == this_thread
         && channels[c].received_message != 0) {
          NUMBER message = { .r = channels[c].received_message };
          channels[c].received_message = 0;
          GPU_MAILBOX_CLIENT__incoming_message( channels[c].client, message );
          last_channel = c;
          if (blocking_message != 0) {
            memory_write_barrier(); // About to write to devices.mailbox
            devices.mailbox[0].config = 1; // Interrupt on not empty
          }
          break;
        }
      }
    }
  }
}

void MBOX__GPU_MAILBOX__register_client( MBOX channel, GPU_MAILBOX_CLIENT client )
{
  claim_lock( &channel->lock );

  if (channel->client.r != 0) {
    release_lock( &channel->lock );
    MBOX__exception( name_code( "Channel already has a client" ).r );
  }

  if (channel->waiting_thread != 0) {
    release_lock( &channel->lock );
    MBOX__exception( name_code( "Channel is already waiting for a response" ).r );
  }

  channel->client = client;

  static uint32_t pump_thread = 0;
  if (pump_thread == 0) {
    static struct {
      uint64_t s[32];
    } pump_thread_stack;
    pump_thread = create_thread( message_pump, (void*) ((&pump_thread_stack)+1) );
  }

  channel->waiting_thread = pump_thread;

  release_lock( &channel->lock );

  MBOX__GPU_MAILBOX__register_client__return();
}

void MBOX__GPU_MAILBOX__queue_message( MBOX channel, NUMBER message )
{
  if (channel->client.r == 0) MBOX__exception( name_code( "Channel has no client" ).r );

  memory_write_barrier(); // About to write to devices.mailbox
  devices.mailbox[0].config = 1; // Not-empty receive interrupts will be handled.

  // We can't wait for a not-full interrupt, the GPU handles interrupts from mailbox 1
  while (0 != (devices.mailbox[1].status & 0x80000000)) {
    blocked_sending_thread = this_thread;
    dsb();
    memory_read_barrier(); // Finished reading from devices.mailbox

if (mapped_memory != 0) {
  mapped_memory[20]++;
}
    sleep_ms( 10 ); // Timeout is just in case all requests were responded to and removed from mailbox 0 between reading status and setting blocked_sending_thread
  }
  memory_read_barrier(); // Finished reading from devices.mailbox

  blocked_sending_thread = 0;
  dsb();
  wake_thread( this_thread ); wait_until_woken(); // clear gate FIXME: lame.

  memory_write_barrier(); // About to write to devices.mailbox
  devices.mailbox[1].value = message.r | (channel - channels);

  MBOX__GPU_MAILBOX__queue_message__return();
}
