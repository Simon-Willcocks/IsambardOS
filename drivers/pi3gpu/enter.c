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
  uint64_t match = DRIVER_SYSTEM__get_core_timer_value( driver_system() ).r + 2000 * ms; // 2000000 ~= 1s
  for (uint64_t now = match - 1; now < match; now = DRIVER_SYSTEM__get_core_timer_value( driver_system() ).r) {
    yield();
  }
}

DRIVER_INITIALISER( entry );

unsigned long long stack_lock = 0;
unsigned long long __attribute__(( aligned( 16 ) )) stack[STACK_SIZE] = { 0x33333333 }; // Just a marker

typedef struct {
  uint64_t lock;
  uint64_t count;
  uint64_t diff;
  uint64_t slowest_response;
} gpu_interrupt_object;

STACK_PER_OBJECT( gpu_interrupt_object, 64 );

typedef union { integer_register r; gpu_interrupt_object *p; } GPU;

static struct gpu_interrupt_object_container __attribute__(( aligned(16) )) gpu_interrupt_handler_singleton = { { 0 }, .object = { .lock = 0, .count = 0x33334444, .diff = 0, .slowest_response = 0xffffffff } };

#include "interfaces/provider/INTERRUPT_HANDLER.h"

ISAMBARD_INTERRUPT_HANDLER__SERVER( GPU )
ISAMBARD_PROVIDER( GPU, AS_INTERRUPT_HANDLER( GPU ) )

ISAMBARD_PROVIDER_UNLOCKED_PER_OBJECT_STACK( GPU )

void GPU__INTERRUPT_HANDLER__interrupt( GPU o )
{
  o.p->count++;

  uint32_t pending = devices.interrupts.IRQ_basic_pending;

  for (int i = 0; i < 32; i++) {
    if (0 != ((1 << i) & pending)) {
      switch (i) {
      case 0:
      {
        o.p->diff = devices.timer.ro_value;
        if (o.p->diff < o.p->slowest_response) {
          o.p->slowest_response = o.p->diff;
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

void map_page( uint64_t physical, void *virtual )
{
  PHYSICAL_MEMORY_BLOCK device_page = DRIVER_SYSTEM__get_device_page( driver_system(), NUMBER_from_integer_register( physical ) );
  DRIVER_SYSTEM__map_at( driver_system(), device_page, NUMBER_from_integer_register( (integer_register) virtual ) );
}

void entry()
{
  map_page( 0x3f00b000, (void*) &devices.unused1 );
  map_page( 0x3f300000, (void*) &devices.emmc );
  map_page( 0x3f200000, (void*) &devices.gpio );
  map_page( 0x3f003000, (void*) &devices.system_timer );

  GPU g = { .p = &gpu_interrupt_handler_singleton.object };
  INTERRUPT_HANDLER obj = GPU_INTERRUPT_HANDLER_to_pass_to( system.r, g );

  DRIVER_SYSTEM__register_interrupt_handler( driver_system(), obj, NUMBER_from_integer_register( 8 ) );

devices.timer.load = (1 << 23) - 1; // Max load value for 23 bit counter
devices.timer.control |= 0x2a2; // Interrupts enabled, but see bit 0 of Enable_Basic_IRQs
//devices.interrupts.Enable_Basic_IRQs |= 1; // FIXME

  expose_frame_buffer();
  // expose_emmc();
}

