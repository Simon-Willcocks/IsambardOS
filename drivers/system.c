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

asm ( "physical_memory_block_service_entry:" ); // FIXME

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

    "\n\tbl map_service_c_code"

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

    "\n0: svc 1" // TODO Throw an exception, recursion not allowed in this case
    "\n\tb 0b"
    "\n.previous" );

#include "drivers.h"
#include "kernel.h"
#include "atomic.h"
#include "system_services.h"

typedef struct { integer_register r; } NUMBER;
#include "interfaces/provider/DRIVER_SYSTEM.h"

static volatile bool board_initialised = false;

unsigned long long stack_lock = 0;
unsigned long long system = 0; // Initialised by _start code
unsigned long long __attribute__(( aligned( 16 ) )) stack[STACK_SIZE] = { 0x33333333 }; // Just a marker

uint64_t __attribute__(( aligned( 16 ) )) map_stack[64];

uint64_t map_stack_lock[4] = { 0 };

extern uint64_t stack_top;
 
void thread_exit()
{
  asm ("wfi");
}

#define INT_STACK_SIZE 64

extern struct {
  uint64_t number;
  uint64_t interrupts_count;
  uint64_t unidentified_interrupts_count;
  uint64_t __attribute__(( aligned( 16 ) )) interrupt_handler_stack[INT_STACK_SIZE];
  // The rest is idle thread stack
} volatile this_core; // Core-specific information

Object memory_manager = 0;

extern integer_register make_special_request( enum Isambard_Special_Request request, ... );

#define MAX_VMBS 64

static uint64_t vmb_lock = 0;
static VirtualMemoryBlock vmbs[MAX_VMBS] = { 0 };

struct service {
  Object provider;
  uint32_t name;
};

static struct service services[50] = { 0 };
static int free_service = 0;

static uint64_t ms_ticks = 0;

/* System (Pi 3) specific code */
static Object interrupt_handlers[12] = { 0 };

void board_remove_interrupt_handler( Object handler, unsigned interrupt )
{
  if (interrupt_handlers[interrupt] != handler) {
    for (;;) { asm volatile ( "svc 1\n\tsvc 3" ); }
  }
  interrupt_handlers[interrupt] = 0;
  // FIXME release handler object
}

void board_register_interrupt_handler( Object handler, unsigned interrupt )
{
  if (interrupt >= 12) {
    // Out of range
    for (;;) { asm volatile ( "svc 1\n\tsvc 3" ); }
  }
  if (interrupt_handlers[interrupt] != 0) {
    // Already claimed
    for (;;) { asm volatile ( "svc 1\n\tsvc 4" ); }
  }
  interrupt_handlers[interrupt] = handler;
  // TODO Fix the caller map to a particular core
  // In other boards, it would be reasonable to enable the relevant interrupt at this point
}

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

static inline void board_call_interrupt_handlers()
{
  asm ( "dsb sy" ); // To protect the AXI bus, individual interrupt handlers don't need to bother
  static uint32_t volatile * const irq_sources = &device_pages.QA7.Core_IRQ_Source[0];

  static uint64_t volatile interrupts_count = 0;
  static uint64_t volatile unidentified_interrupts_count = 0;
  interrupts_count++;
  this_core.interrupts_count++;

  // This is the first level of Pi interrupts, the GPU interrupt may happen for many reasons,
  // dealt with in the driver.
  dsb();
  uint32_t sources = irq_sources[this_core.number];

  static uint32_t interrupts_handled[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

  if (sources == 0) {
interrupts_handled[0]+= 0x100000;
    dsb();
    sources = irq_sources[this_core.number];
    if (sources == 0) {
      this_core.unidentified_interrupts_count++;
      unidentified_interrupts_count++;
      // sources = 0xfff; // Try all the handlers, they should be able to cope with no interrupt
    }
  }

  for (int i = 0; sources != 0 && i < 12; i++) {
    if (0 != ((1 << i) & sources)) {
      if (i == 11) {
        device_pages.QA7.Local_timer_write_flags = (1 << 31);
	ms_ticks ++;
      }
      if (interrupt_handlers[i] != 0) {
interrupts_handled[i]+=16;
        inter_map_procedure_0p( interrupt_handlers[i], 0 );
      }
      else {
interrupts_handled[i]+= 0x10000;
      }
    }

    // FIXME sources = sources & ~(1 << i);
  }
  asm ( "dsb sy" ); // To protect the AXI bus, individual interrupt handlers don't need to bother
  asm ( "svc 0" ); // Only while debugging FIXME
}

uint64_t core_timer_value()
{
  uint64_t result = device_pages.QA7.Core_timer_access_LS_32_bits;
  result |= ((uint64_t) device_pages.QA7.Core_timer_access_MS_32_bits) << 32;
  return result;
}

uint32_t initial_interrupts_routing = (uint32_t) -1;

void board_initialise()
{
  make_special_request( Isambard_System_Service_Add_Device_Page, 0x40000000, (uint64_t) &device_pages.QA7 );
  initial_interrupts_routing = device_pages.QA7.GPU_interrupts_routing;
  device_pages.QA7.GPU_interrupts_routing = 0; // IRQ and FIQ to Core 0
}

/* End board specific code */

#include "interfaces/provider/DRIVER_SYSTEM.h"

uint64_t map_service_c_code( uint64_t o, uint32_t call, uint64_t p1, uint64_t p2, uint64_t p3 )
{
  MapValue mv = { .raw = o };

  switch (call) {
  case DRIVER_SYSTEM_get_device_page:
    {
      ContiguousMemoryBlock cmb = { .start_page = p1 >> 12,
                                    .page_count = 1,
                                    .memory_type = Device_nGnRnE };
      return object_to_return( (void*) System_Service_PhysicalMemoryBlock, cmb.raw );
    }
    break;
  case DRIVER_SYSTEM_get_physical_memory_block:
    {
      ContiguousMemoryBlock cmb = { .start_page = p1 >> 12,
                                    .page_count = p2 >> 12,
                                    .memory_type = Fully_Cacheable };
      return object_to_return( (void*) System_Service_PhysicalMemoryBlock, cmb.raw );
    }
    break;
  case DRIVER_SYSTEM_map_at:
    {
      claim_lock( &vmb_lock );
            // FIXME Expand number of blocks
            // FIXME Allow for addresses not in order
            // FIXME Invalid parameters
      ContiguousMemoryBlock cmb;
      cmb.raw = make_special_request( Isambard_System_Service_ReadInterface, p1 );

      make_special_request( Isambard_System_Service_ReadHeap, mv.heap_offset_lsr4 << 4, mv.number_of_vmbs * sizeof( VirtualMemoryBlock ), vmbs );

      for (int i = 0; i < mv.number_of_vmbs; i++) {
        if (vmbs[i].page_count == 0) {
          vmbs[i].start_page = p2 >> 12;
          vmbs[i].page_count = cmb.page_count;
          vmbs[i].read_only = 0;
          vmbs[i].executable = 0;
          vmbs[i].memory_block = p1;
          vmbs[i+1].raw = 0;
          break;
        }
      }

      make_special_request( Isambard_System_Service_WriteHeap, mv.heap_offset_lsr4 << 4, mv.number_of_vmbs * sizeof( VirtualMemoryBlock ), vmbs );
      release_lock( &vmb_lock );
      return 0;
    }
  case DRIVER_SYSTEM_create_thread:
    {
      return make_special_request( Isambard_System_Service_Create_Thread, p1, p2, p3 );
    }
    break;
  case DRIVER_SYSTEM_physical_address_of:
    // This function is implemented at el1;
    break;
  case DRIVER_SYSTEM_register_service:
    {
      struct service *s = &services[free_service++]; // FIXME limit!
      s->provider = p2;
      s->name = p1;
      return 0;
    }
    break;
  case DRIVER_SYSTEM_get_service:
    {
      struct service *s = services;
      while (s < &services[free_service]) {
        if (s->name == p1) {
          return duplicate_to_return( s->provider );
        }
      }
      return 0;
    }
  case DRIVER_SYSTEM_get_core_interrupts_count:
    {
      return this_core.interrupts_count;
    }
    break;
  case DRIVER_SYSTEM_get_ms_timer_ticks:
    {
      return ms_ticks;
    }
    break;
  case DRIVER_SYSTEM_get_core_timer_value:
    {
      return core_timer_value();
    }
    break;
  case DRIVER_SYSTEM_register_interrupt_handler:
    {
      board_register_interrupt_handler( p1, p2 );
      return 0;
    }
    break;
  case DRIVER_SYSTEM_remove_interrupt_handler:
    {
      board_remove_interrupt_handler( p1, p2 );
      return 0;
    }
    break;
  }
  for (;;) { asm volatile ( "svc 1\n\tsvc 2" ); }
}

#if 0
typedef MapExportValue DS;

ISAMBARD_DRIVER_SYSTEM__SERVER( DS );

static PHYSICAL_MEMORY_BLOCK DS__DRIVER_SYSTEM__get_device_page( DS o, unsigned call, NUMBER physical_address )
{
  ContiguousMemoryBlock cmb = { .start_page = physical_address >> 12,
                                .page_count = 1,
                                .memory_type = Device_nGnRnE,
                                .is_contiguous_memory_block = 0 };
  return object_to_return( (void*) System_Service_PhysicalMemoryBlock, cmb.raw );
}

static PHYSICAL_MEMORY_BLOCK DS__DRIVER_SYSTEM__get_physical_memory_block( DS o, unsigned call, NUMBER start, NUMBER size )
{
}

static void DS__DRIVER_SYSTEM__map_at( DS o, unsigned call, PHYSICAL_MEMORY_BLOCK block, NUMBER start )
{
}

static NUMBER DS__DRIVER_SYSTEM__create_thread( DS o, unsigned call, NUMBER code, NUMBER stack_top )
{
}

static NUMBER DS__DRIVER_SYSTEM__physical_address_of( DS o, unsigned call, NUMBER va )
{
}

static void DS__DRIVER_SYSTEM__register_service( DS o, unsigned call, NUMBER name_crc, NUMBER provider )
{
}

static SERVICE DS__DRIVER_SYSTEM__get_service( DS o, unsigned call, NUMBER name_crc )
{
}

static NUMBER DS__DRIVER_SYSTEM__get_core_interrupts_count( DS o, unsigned call )
{
}

static NUMBER DS__DRIVER_SYSTEM__get_ms_timer_ticks( DS o, unsigned call )
{
}

static NUMBER DS__DRIVER_SYSTEM__get_core_timer_value( DS o, unsigned call )
{
}

static void DS__DRIVER_SYSTEM__register_interrupt_handler( DS o, unsigned call, INTERRUPT_HANDLER handler, NUMBER interrupt )
{
}

static void DS__DRIVER_SYSTEM__remove_interrupt_handler( DS o, unsigned call, INTERRUPT_HANDLER handler, NUMBER interrupt )
{
}

#endif

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
  device_pages.QA7.Local_Interrupt_routing0 = 0; // Direct local timer interrupt to CPU 0 IRQ

  device_pages.QA7.timer_prescaler = 0x06AAAAAB; // 19.2... somethings
  device_pages.QA7.control = (1 << 8); // Timer enable (increment in ones)

  // Reasonably close to 1ms ticks: 19200000/500
  device_pages.QA7.Local_timer_control_and_status = (1 << 29) | (1 << 28) | 19200000/500; // Enable timer, interrupt.
  device_pages.QA7.Local_timer_write_flags = (1 << 31) | (1 << 30); // Clear IRQ and load timer
  //device_pages.QA7.Local_timer_control_and_status = (1 << 29) | (1 << 28) | ((1 << 28) - 1); // Enable timer, and interrupt. Longest timeout

  device_pages.QA7.Core_IRQ_Source[0] = 0x3ff; // (1 << 8);
  device_pages.QA7.Core_IRQ_Source[1] = 0x3ff; // (1 << 8);
  device_pages.QA7.Core_IRQ_Source[2] = 0x3ff; // (1 << 8);
  device_pages.QA7.Core_IRQ_Source[3] = 0x3ff; // (1 << 8);
}

void __attribute__(( noreturn )) idle_thread_entry( Object system_interface,
                                                    Object memory_manager_map,
						    uint32_t core_number,
						    uint64_t free_memory_start,
						    uint64_t free_memory_end )
{
  this_core.number = core_number;

  if (core_number == 0) {
    system = system_interface;
    memory_manager = memory_manager_map;

    board_initialise();

    inter_map_procedure_2p( memory_manager, 0, free_memory_start, free_memory_end ); // Initialise

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
    start_ms_timer();
  }

  for (;;) {
    uint64_t time = core_timer_value();
    if (time == 0) {
      asm volatile ( "svc 3" );
    }
    uint64_t othertime;
//    asm volatile( "mrs %[ot], CNTPCT_EL0" : [ot] "=r" (othertime) );
    for (int i = 0; i < 100000; i++) {
      if (!yield())
      {
        //asm volatile ( "svc 2" );
      }
    }
    if (core_timer_value() == time) {
      asm volatile ( "svc 8" );
    }
  }

  __builtin_unreachable();
}

