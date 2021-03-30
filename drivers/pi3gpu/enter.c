/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer, GPU timers, and GPIO.
//
// Current limitations:.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice with anyone else using the mailbox

#include "devices.h"

ISAMBARD_INTERFACE( GPU_MAILBOX_MANAGER )
ISAMBARD_INTERFACE( GPU_MAILBOX )
ISAMBARD_INTERFACE( GPU_MAILBOX_CLIENT )

// Both intimitely related interfaces in the one file
#include "interfaces/client/GPU_MAILBOX.h"

ISAMBARD_STACK( interrupt_stack, 64 );

typedef struct {
  uint64_t lock;
  uint64_t count;
  uint64_t diff;
  uint64_t slowest_response;
  uint32_t all_ints;
} gpu_interrupt_object;

static gpu_interrupt_object gpu_interrupt_handler_singleton __attribute__(( aligned(16) )) = { .count = 0, .diff = 0, .slowest_response = 0xffffffff };

typedef union { integer_register r; gpu_interrupt_object *p; } GPU;

#include "interfaces/provider/INTERRUPT_HANDLER.h"

ISAMBARD_INTERRUPT_HANDLER__SERVER( GPU )
ISAMBARD_PROVIDER( GPU, AS_INTERRUPT_HANDLER( GPU ) )

ISAMBARD_PROVIDER_NO_LOCK_AND_SINGLE_STACK( GPU, RETURN_FUNCTIONS_INTERRUPT_HANDLER( GPU ), interrupt_stack, 64*8 )

void GPU__INTERRUPT_HANDLER__interrupt( GPU o )
{
  o.p->count++;

  uint32_t pending = devices.interrupts.IRQ_basic_pending;

  o.p->all_ints |= pending;

  memory_read_barrier(); // Completed our reads of devices.interrupts

  for (int i = 0; i < 32; i++) {
    if (0 != ((1 << i) & pending)) {
      switch (i) {
      case 0:
      {
        o.p->diff = devices.timer.ro_value;
        memory_read_barrier(); // Completed our reads of devices.timer
        if (o.p->diff < o.p->slowest_response) {
          o.p->slowest_response = o.p->diff;
        }
        memory_write_barrier(); // About to write to devices.timer
        devices.timer.wo_irq_clear = 1; // Any value should do it.
extern void timer_event();
        timer_event();
      }
      break;
      case 1:
      {
        mailbox_interrupt();
      }
      break;
      case 20:
      {
        emmc_interrupt();
      }
      break;
      default:
        asm ( "svc 0" );
        for (;;) { asm ( "brk 2" ); }
      }
    }
  }

  GPU__INTERRUPT_HANDLER__interrupt__return();
}

void map_page( uint64_t physical, void *virtual )
{
  PHYSICAL_MEMORY_BLOCK device_page = DRIVER_SYSTEM__get_device_page( driver_system(), NUMBER__from_integer_register( physical ) );
  DRIVER_SYSTEM__map_at( driver_system(), device_page, NUMBER__from_integer_register( (integer_register) virtual ) );
}

void entry()
{
  map_page( 0x3f00b000, (void*) &devices.unused1 );
  map_page( 0x3f300000, (void*) &devices.emmc );
  map_page( 0x3f200000, (void*) &devices.gpio );
  map_page( 0x3f003000, (void*) &devices.system_timer );

  INTERRUPT_HANDLER obj = GPU__INTERRUPT_HANDLER__to_pass_to( system.r, &gpu_interrupt_handler_singleton );

  DRIVER_SYSTEM__register_interrupt_handler( driver_system(), obj, NUMBER__from_integer_register( 8 ) );

  expose_gpu_mailbox(); // Needed by FB, EMMC

  memory_write_barrier(); // About to write to devices.interrupts
  // devices.interrupts.Enable_Basic_IRQs = 1; // Enable "ARM Timer" IRQ
  devices.interrupts.Enable_Basic_IRQs = 2; // Enable "ARM Mailbox" IRQ

  expose_frame_buffer();
  expose_emmc();
}

