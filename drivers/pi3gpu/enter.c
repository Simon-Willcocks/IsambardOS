/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer, GPU timers, and GPIO.
//
// Current limitations:.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice with anyone else using the mailbox

#include "devices.h"

void sleep_ms( uint64_t ms ) // FIXME
{
  uint64_t match = get_core_timer_value() + 2000 * ms; // 2000000 ~= 1s
  for (uint64_t now = match - 1; now < match; now = get_core_timer_value()) {
    yield();
  }
}

DRIVER_INITIALISER( entry );

unsigned long long stack_lock = 0;
unsigned long long system = 0; // Initialised by _start code
unsigned long long __attribute__(( aligned( 16 ) )) stack[STACK_SIZE] = { 0x33333333 }; // Just a marker

Object gpu_interrupt_handler_object = 0;

typedef struct {
  uint64_t lock;
  uint64_t count;
  uint64_t diff;
  uint64_t slowest_response;
} gpu_interrupt_object;

STACK_PER_OBJECT( gpu_interrupt_object, 64 );
SIMPLE_CALL_VENEER( gpu_interrupt );

// FIXME Other possible calls: shutting down?
int gpu_interrupt_handler( gpu_interrupt_object *object, uint64_t call )
{
  object->count++;

  if (call == 0) {
    // Interrupt
    uint32_t pending = devices.interrupts.IRQ_basic_pending;

    for (int i = 0; i < 32; i++) {
      if (0 != ((1 << i) & pending)) {
        switch (i) {
        case 0:
        {
          object->diff = devices.timer.ro_value;
          if (object->diff < object->slowest_response) {
            object->slowest_response = object->diff;
          }
          devices.timer.wo_irq_clear = 1; // Any value should do it.

          asm ( "svc 0" ); // FIXME Remove when everything working
        }
        break;
        case 20:
        {
          emmc_interrupt();
        }
        break;
        default:
          asm ( "svc 0" );
          for (;;) { asm ( "svc 2" ); }
        }
      }
    }
  }
  else {
    asm ( "svc 0" );
    for (;;) { asm ( "svc 2" ); }
  }

  return 0;
}

static struct gpu_interrupt_object_container __attribute__(( aligned(16) )) gpu_interrupt_handler_singleton = { { 0 }, .object = { .lock = 0, .count = 0x33334444, .diff = 0, .slowest_response = 0xffffffff } };

void map_page( uint64_t physical, void *virtual )
{
  Object device_page = get_device_page( physical );
  map_physical_block_at( device_page, (uint64_t) virtual );
}

void entry()
{
  map_page( 0x3f00b000, (void*) &devices.unused1 );
  map_page( 0x3f300000, (void*) &devices.emmc );
  map_page( 0x3f200000, (void*) &devices.gpio );
  map_page( 0x3f003000, (void*) &devices.system_timer );

  gpu_interrupt_handler_object = object_to_pass_to( system, gpu_interrupt_veneer, (uint64_t) &gpu_interrupt_handler_singleton.object.lock );

  register_interrupt_handler( gpu_interrupt_handler_object, 8 );

devices.timer.load = (1 << 23) - 1; // Max load value for 23 bit counter
devices.timer.control |= 0x2a2; // Interrupts enabled, but see bit 0 of Enable_Basic_IRQs

  expose_frame_buffer();
  expose_emmc();

  for (;;) { yield(); } // Can I return, yet?
  // END DEBUG
}

