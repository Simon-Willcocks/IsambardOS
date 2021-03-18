/* Copyright (c) 2020 Simon Willcocks */

// This driver controls the memory, maps and threads of an Isambard system.
// It runs in Secure mode, at EL0.
// It is the only driver to be able to make requests of the physical memory manager.
// It is the only driver to be able to make special requests of the kernel.
// These two drivers are the only drivers to have special entry points
// The special entry points for this driver are: start_entry, thread_exit, map_service_entry,
// and physical_memory_block_service_entry

// Each core has one page of its own local storage and stack mapped to the same address
// The memory is part of the Core structure, but does not expose the structures to this code.
// The kernel will never interpret the content of this memory.

asm ( ".section .init"
    "\n.global _start"
    "\n.type _start, %function"

    // Standard entry points
    "\n_start:"
    "\n\tb start_entry"
    "\n\tb thread_exit"
    "\n\tb map_service_entry"
    "\n\tb physical_memory_block_service_entry"

    "\nstart_entry:"
    "\n\tadrp x9, this_core + 4096"
    "\n\tmov  sp, x9"
    "\n\tb    idle_thread_entry" // Never returns
    "\n.previous" );

asm ( ".section .text"
    "\nmake_special_request:"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xffff"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\n.previous" );

asm ( "physical_memory_block_service_entry: wfi" ); // FIXME

asm (
    ".section .text"
    "\nmap_service_entry:"
    "\n\tadr  x17, map_stack_lock"
    "\n2:" // Loop
    "\n\tldxr x16, [x17]"

    "\n\tcbnz x16, 1f" // already held by a thread, possibly this one

    // No-one has the lock, claim it
    "\n\tstxr w16, x18, [x17]"
"\n\tstr x18, [x17, #8]"
    "\n\tcbnz w16, 2b" // Write failed

    "\n3:" // Lock claimed
    "\n\tadr x17, map_stack + 8 * 64"
    "\n\tmov sp, x17"

    "\n\tbl MapValue_call_handler"

    // Release
    "\n\tadr  x17, map_stack_lock"
    "\n\tldxr x16, [x17]"

    "\n\tcmp x16, x18"
    "\n\tbeq 0f"
    "\n4:"
    "\n\tsvc 0xfffb" // Need kernel help to release
    "\n\tsvc 0xfffd" // Return to caller

    "\n0:"
    "\n\tstxr w16, xzr, [x17]"
"\n\tstr xzr, [x17, #16]"
    "\n\tcbnz w16, 4b" // Released it, or need help?
    "\n\tsvc 0xfffd" // Return to caller

    "\n1:"
    "\n\tcmp w16, w18"
    "\n\tclrex"
    "\n\tbeq 0f"
    "\n\tsvc 0xfffa" // Need kernel help to claim
    "\n\tb 3b"

    "\n0: brk 1" // TODO Throw an exception, recursion not allowed in this case
    "\n\tb 0b"
    "\n.previous" );

#include "drivers.h"
#include "atomic.h"
#include "system_services.h"
#include "aarch64_vmsa.h"

// List all the interfaces known to this file, and to those interfaces

#include "interfaces/client/PHYSICAL_MEMORY_BLOCK.h"
#include "interfaces/client/INTERRUPT_HANDLER.h"
// ContiguousMemoryBlock implements this interface
#include "interfaces/provider/PHYSICAL_MEMORY_BLOCK.h"
// MapValue implements both of these interfaces
#include "interfaces/provider/SYSTEM.h"
#include "interfaces/provider/DRIVER_SYSTEM.h"

ISAMBARD_PHYSICAL_MEMORY_BLOCK__SERVER( ContiguousMemoryBlock )
ISAMBARD_PROVIDER( ContiguousMemoryBlock, AS_PHYSICAL_MEMORY_BLOCK( ContiguousMemoryBlock ) )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( ContiguousMemoryBlock, map_stack_lock, map_stack, 64 * 8 )

ISAMBARD_SYSTEM__SERVER( MapValue )
ISAMBARD_DRIVER_SYSTEM__SERVER( MapValue )
ISAMBARD_PROVIDER( MapValue, AS_DRIVER_SYSTEM( MapValue ); AS_SYSTEM( MapValue ) )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( MapValue, map_stack_lock, map_stack, 64 * 8 )

static volatile bool board_initialised = false;

uint64_t __attribute__(( aligned( 16 ) )) map_stack[64];

uint64_t map_stack_lock = 0;

extern uint64_t stack_top;
 
void thread_exit()
{
        for (;;) { asm ( "mov x0, #0\n\tmov x1, #0\n\tsvc 0xfff5" ); } // FIXME This code has no stack.
  // Put into a container, for re-starting later...?
  //for (;;) { wait_until_woken(); asm ( "brk 12" ); }
}

#define INT_STACK_SIZE 64

static uint32_t ticks_per_millisecond = 0;

extern struct {
  uint64_t number;
  uint64_t last_cval;
  uint64_t interrupts_count;
  uint64_t unidentified_interrupts_count;
  uint64_t __attribute__(( aligned( 16 ) )) interrupt_handler_stack[INT_STACK_SIZE];
  // The rest is idle thread stack
} this_core; // Core-specific information

Object memory_manager = 0;

extern integer_register make_special_request( enum Isambard_Special_Request request, ... );

#define MAX_VMBS 64

static uint64_t vmb_lock = 0;
static VirtualMemoryBlock vmbs[MAX_VMBS] = { 0 };

struct service {
  NUMBER service;
  NUMBER type_crc;
  NUMBER name_crc;
};

static struct service services[50] = { 0 };
static int free_service = 0;

static uint64_t ms_ticks = 0;

/* System (Pi 3) specific code */
static INTERRUPT_HANDLER interrupt_handlers[12] = { { .r = 0 } };

void board_remove_interrupt_handler( INTERRUPT_HANDLER handler, unsigned interrupt )
{
  if (interrupt_handlers[interrupt].r != handler.r) {
    for (;;) { asm volatile ( "svc 1\n\tbrk 3" ); }
  }
  interrupt_handlers[interrupt].r = 0;
  // FIXME release handler object
}

void board_register_interrupt_handler( INTERRUPT_HANDLER handler, unsigned interrupt )
{
  if (interrupt >= 12) {
    // Out of range
    for (;;) { asm volatile ( "svc 1\n\tbrk 3" ); }
  }
  if (interrupt_handlers[interrupt].r != 0) {
    // Already claimed
    for (;;) { asm volatile ( "svc 1\n\tbrk 4" ); }
  }
  interrupt_handlers[interrupt] = handler;
  // TODO Fix the caller map to a particular core
  // In other boards, it would be reasonable to enable the relevant interrupt at this point
}

// We have one device, in this driver
extern struct {
  union {
    struct {
      uint32_t       control;
      uint32_t       res1;
      uint32_t       timer_prescaler;
      uint32_t       GPU_interrupts_routing;
      uint32_t       Performance_Monitor_Interrupts_routing_set;
      uint32_t       Performance_Monitor_Interrupts_routing_clear;
      uint32_t       res2;
      uint32_t       Core_timer_access_LS_32_bits; // Access first when reading/writing 64 bits.
      uint32_t       Core_timer_access_MS_32_bits;
      uint32_t       Local_Interrupt_routing0;
      uint32_t       Local_Interrupts_routing1;
      uint32_t       Axi_outstanding_counters;
      uint32_t       Axi_outstanding_IRQ;
      uint32_t       Local_timer_control_and_status;
      uint32_t       Local_timer_write_flags;
      uint32_t       res3;
      uint32_t       Core_timers_Interrupt_control[4];
      uint32_t       Core_Mailboxes_Interrupt_control[4];
      uint32_t       Core_IRQ_Source[4];
      uint32_t       Core_FIQ_Source[4];
      struct {
        uint32_t       Mailbox[4]; // Write only!
      } Core_write_set[4];
      struct {
        uint32_t       Mailbox[4]; // Read/write
      } Core_write_clear[4];
    } QA7;
    struct { uint32_t page[1024]; };
  };
} volatile __attribute__(( aligned( 4096 ) )) device_pages;

uint32_t all_interrupts  = 0;

static inline void board_call_interrupt_handlers()
{
  static uint64_t volatile interrupts_count = 0;
  static uint64_t volatile unidentified_interrupts_count = 0;
  interrupts_count++;
  this_core.interrupts_count++;

  // This is the first level of Pi interrupts, the GPU interrupt may happen for many reasons,
  // dealt with in the driver.

  uint32_t sources = device_pages.QA7.Core_IRQ_Source[this_core.number];
all_interrupts |= sources;
  static uint32_t interrupts_handled[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

  if (sources == 0) {
interrupts_handled[0]+= 0x100000;
    dsb();
    sources = device_pages.QA7.Core_IRQ_Source[this_core.number];
    if (sources == 0) {
      this_core.unidentified_interrupts_count++;
      unidentified_interrupts_count++;
      // sources = 0xfff; // Try all the handlers, they should be able to cope with no interrupt
    }
  }

  memory_read_barrier(); // Completed our reads of device_pages.QA7

  if (0 != (sources & 2)) { // Timer interrupt
    this_core.last_cval += ticks_per_millisecond;
    asm ( "msr CNTP_CVAL_EL0, %[d]" : : [d] "r" (this_core.last_cval) );
    ms_ticks ++;
    gate_function( 0, 0 ); // Special case for interrupt handler thread, releases all threads that timeout this tick
  }

  for (int i = 0; sources != 0 && i < 12; i++) {
    if (0 != ((1 << i) & sources)) {
      if (interrupt_handlers[i].r != 0) {
interrupts_handled[i]+=16;
        INTERRUPT_HANDLER__interrupt( interrupt_handlers[i] );
      }
      else {
interrupts_handled[i]+= 0x10000;
      }
    }

    sources = sources & ~(1 << i);
  }
}

uint64_t core_timer_value()
{
  uint64_t result = device_pages.QA7.Core_timer_access_LS_32_bits;
  result |= ((uint64_t) device_pages.QA7.Core_timer_access_MS_32_bits) << 32;
  memory_read_barrier(); // Completed our reads of device_pages.QA7
  return result;
}

uint32_t initial_interrupts_routing = (uint32_t) -1;

void board_initialise()
{
  make_special_request( Isambard_System_Service_Add_Device_Page, 0x40000000, (uint64_t) &device_pages.QA7 );
  initial_interrupts_routing = device_pages.QA7.GPU_interrupts_routing;
  memory_read_barrier(); // Completed our reads of device_pages.QA7
  memory_write_barrier(); // About to write to device_pages.QA7
  device_pages.QA7.GPU_interrupts_routing = 0; // IRQ and FIQ to Core 0
}

/* End board specific code */

PHYSICAL_MEMORY_BLOCK MapValue__DRIVER_SYSTEM__get_device_page( MapValue o, NUMBER physical_address )
{
  o = o;
  ContiguousMemoryBlock cmb = { .start_page = physical_address.r >> 12,
                                .page_count = 1,
                                .memory_type = Device_nGnRnE };
  // Don't use the ContiguousMemoryBlock_PHYSICAL_MEMORY_BLOCK_to_return routine, the handler must be the
  // special value for the kernel to recognise it.
  PHYSICAL_MEMORY_BLOCK result;
  result.r = object_to_return( (void*) System_Service_PhysicalMemoryBlock, cmb.r );
  return result;
}

PHYSICAL_MEMORY_BLOCK MapValue__DRIVER_SYSTEM__get_physical_memory_block( MapValue o, NUMBER start, NUMBER size )
{
  o = o;
  ContiguousMemoryBlock cmb = { .start_page = start.r >> 12,
                                .page_count = size.r >> 12,
                                .memory_type = Fully_Cacheable };
  // Don't use the ContiguousMemoryBlock_PHYSICAL_MEMORY_BLOCK_to_return routine, the handler must be the
  // special value for the kernel to recognise it.
  PHYSICAL_MEMORY_BLOCK result;
  result.r = object_to_return( (void*) System_Service_PhysicalMemoryBlock, cmb.r );
  return result;
}

void MapValue__DRIVER_SYSTEM__map_at( MapValue o, PHYSICAL_MEMORY_BLOCK block, NUMBER start )
{
  claim_lock( &vmb_lock );
        // FIXME Expand number of blocks
        // FIXME Allow for addresses not in order
        // FIXME Invalid parameters
  ContiguousMemoryBlock cmb;
  cmb.r = make_special_request( Isambard_System_Service_ReadInterface, block );

  make_special_request( Isambard_System_Service_ReadHeap, o.heap_offset_lsr4 << 4, o.number_of_vmbs * sizeof( VirtualMemoryBlock ), vmbs );

  for (int i = 0; i < o.number_of_vmbs; i++) {
    if (vmbs[i].page_count == 0) {
      vmbs[i].start_page = start.r >> 12;
      vmbs[i].page_count = cmb.page_count;
      vmbs[i].read_only = 0;
      vmbs[i].executable = 0;
      vmbs[i].memory_block = block.r;
      vmbs[i+1].r = 0;
      break;
    }
  }

  make_special_request( Isambard_System_Service_WriteHeap, o.heap_offset_lsr4 << 4, o.number_of_vmbs * sizeof( VirtualMemoryBlock ), vmbs );
  release_lock( &vmb_lock );
}

NUMBER MapValue__SYSTEM__create_thread( MapValue o, NUMBER code, NUMBER stack_top )
{
  o = o;
  return NUMBER_from_integer_register( make_special_request( Isambard_System_Service_Create_Thread, code.r, stack_top.r, 0 ) );
}

NUMBER MapValue__DRIVER_SYSTEM__physical_address_of( MapValue o, NUMBER va )
{
  o = o; va = va;
  for (;;) { asm( "wfi" ); }
  // This call is intercepted by the kernel, since it requires EL1 privileges
  return NUMBER_from_integer_register( unknown_call( DRIVER_SYSTEM_physical_address_of ) ); // FIXME
}

void MapValue__SYSTEM__register_service( MapValue o, NUMBER name_crc, NUMBER service, NUMBER type_crc )
{
  o = o;
  struct service *s = &services[free_service++]; // FIXME limit! LOCK!!!
  s->name_crc = name_crc;
  s->type_crc = type_crc;
  s->service = service;
}

NUMBER MapValue__SYSTEM__get_service( MapValue o, NUMBER name_crc, NUMBER type_crc, NUMBER timeout )
{
  o = o;
  struct service *s = services;
  while (s < &services[free_service]) {
    if (s->name_crc.r == name_crc.r && (type_crc.r == 0 || type_crc.r == s->type_crc.r)) {
      return NUMBER_from_integer_register( duplicate_to_return( s->service.r ) );
    }
    s++;
  }
  return NUMBER_from_integer_register( 0 );
}

PHYSICAL_MEMORY_BLOCK MapValue__SYSTEM__allocate_memory( MapValue o, NUMBER size )
{
  o = o;
  PHYSICAL_MEMORY_BLOCK result = { 0 };

  integer_register r = Isambard_11( memory_manager, 1, size.r ); // Allocate

  if (r != 0) {
    ContiguousMemoryBlock cmb = { .start_page = r >> 12,
                                  .page_count = size.r >> 12,
                                  .memory_type = Fully_Cacheable };
    // Don't use the ContiguousMemoryBlock_PHYSICAL_MEMORY_BLOCK_to_return routine, the handler must be the
    // special value for the kernel to recognise it.
    result.r = object_to_return( (void*) System_Service_PhysicalMemoryBlock, cmb.r );
  }

  return result;
}

NUMBER MapValue__DRIVER_SYSTEM__get_core_interrupts_count( MapValue o )
{
  o = o;
  return NUMBER_from_integer_register( this_core.interrupts_count );
}

NUMBER MapValue__DRIVER_SYSTEM__get_ms_timer_ticks( MapValue o )
{
  o = o;
  return NUMBER_from_integer_register( ms_ticks );
}

NUMBER MapValue__DRIVER_SYSTEM__get_core_timer_value( MapValue o )
{
  o = o;
  return NUMBER_from_integer_register( all_interrupts );
  return NUMBER_from_integer_register( core_timer_value() );
}

void MapValue__DRIVER_SYSTEM__register_interrupt_handler( MapValue o, INTERRUPT_HANDLER handler, NUMBER interrupt )
{
  o = o;
  board_register_interrupt_handler( handler, interrupt.r );
}

void MapValue__DRIVER_SYSTEM__remove_interrupt_handler( MapValue o, INTERRUPT_HANDLER handler, NUMBER interrupt )
{
  o = o;
  board_remove_interrupt_handler( handler, interrupt.r );
}

extern void subsequent_core_system_thread();

void __attribute__(( noreturn )) interrupt_handler_thread()
{
  for (;;) {
    make_special_request( Isambard_System_Service_Set_Interrupt_Thread );
    board_call_interrupt_handlers();
  }
}

void create_interrupt_handler_thread()
{
  // This cannot be a call via the map, because it is called by the idle thread, which
  // is never allowed to block, lest it risk there being no runnable threads for this core.
  make_special_request( Isambard_System_Service_Create_Thread, interrupt_handler_thread, (uint64_t*) &this_core.interrupt_handler_stack[INT_STACK_SIZE], 0 );
}

static void start_ms_timer()
{
  memory_write_barrier(); // About to write to device_pages.QA7
  device_pages.QA7.Local_Interrupt_routing0 = 0; // Direct local timer interrupt to CPU 0 IRQ

  //device_pages.QA7.timer_prescaler = 0x06AAAAAB; // 19.2... somethings
  device_pages.QA7.timer_prescaler = 0x80000000;
  device_pages.QA7.control = (1 << 8); // Timer associated with APB clock (not Crystal clock) (increment in ones)

  // Reasonably close to 1ms ticks: 19200000/500
  // device_pages.QA7.Local_timer_control_and_status = (1 << 29) | (1 << 28) | 19200000/500; // Enable timer, interrupt.
  // device_pages.QA7.Local_timer_control_and_status = (1 << 29) | (1 << 28) | ((1 << 28) - 1); // Enable timer, and interrupt. Longest timeout

  // device_pages.QA7.Local_timer_control_and_status = (1 << 29) | (1 << 28) | 100000; // Enable timer, interrupt.  ~10x per second?
  device_pages.QA7.Local_timer_control_and_status = (1 << 28) | 100000; // Enable timer, no interrupt
  device_pages.QA7.Local_timer_write_flags = (1 << 31) | (1 << 30); // Clear IRQ and load timer

  device_pages.QA7.Core_IRQ_Source[0] = 0xffd; // (1 << 8);
  device_pages.QA7.Core_IRQ_Source[1] = 0x00d; // (1 << 8);
  device_pages.QA7.Core_IRQ_Source[2] = 0x00d; // (1 << 8);
  device_pages.QA7.Core_IRQ_Source[3] = 0x00d; // (1 << 8);

  device_pages.QA7.Core_timers_Interrupt_control[this_core.number] = 2; // The interrupt that's triggered by the generic ARM timer

  // Establish 1ms timer, using generic timer (implemented in QEMU, unlike the above)
  uint64_t now;
  uint32_t frequency;
  asm ( "mrs %[freq], CNTFRQ_EL0" : [freq] "=r" (frequency) );
  ticks_per_millisecond = frequency / 1000;

  asm ( "mrs %[d], CNTPCT_EL0" : [d] "=r" (now) );
  this_core.last_cval = now + ticks_per_millisecond; // Match in a millisecond
  asm ( "msr CNTP_CVAL_EL0, %[d]" : : [d] "r" (this_core.last_cval) );
  asm ( "msr CNTP_CTL_EL0, %[w]" : : [w] "r" (5) ); // Enable interrupts
}

void __attribute__(( noreturn )) idle_thread_entry( Object system_interface,
                                                    Object memory_manager_map,
						    uint32_t core_number,
						    uint64_t free_memory_start,
						    uint64_t free_memory_end )
{
  system_interface = system_interface; // Already stored in system

  this_core.number = core_number;

  if (core_number == 0) {
    memory_manager = memory_manager_map;

    board_initialise();

    Isambard_20( memory_manager, 0, free_memory_start, free_memory_end ); // Initialise

    board_initialised = true;

    asm volatile ( "dsb sy\n\tsev" );
  }
  else {
    while (!board_initialised) { asm volatile ( "wfe" ); }
    // Get to be able to see devices mapped during initialisation
    make_special_request( Isambard_System_Service_Updated_Map, 0x5577557788558855ull );
  }

  create_interrupt_handler_thread();

  if (core_number == 0) {
    // Other timers will be available, but 1ms seems reasonable for timing events,
    // considering a 2MHz computer used 10ms ticks, in 1982.
    start_ms_timer();
  }

  for (;;) {
    if (!yield())
    {
      // Nothing else running on this core.
      // TODO: Ask other cores if there's something we can do
      asm volatile ( "wfi" );
    }
  }

  __builtin_unreachable();
}

