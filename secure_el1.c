/* Copyright (c) 2020 Simon Willcocks */

/* This file contains all the code that will run under Secure EL1.
 * It has access to all memory, initially, then maps itself to high memory.
 *
 */

#define UNUSED( v ) asm volatile ( "" :: "r" (v) )

#include "kernel.h"
#include "system_services.h"
#include "atomic.h"

static const interface_index system_map_index = 1;
static const interface_index memory_allocator_map_index = 2;
static const unsigned number_of_system_maps = 2;

uint64_t VTTBR_EL2 = 0;

// build.sh generated:
#include "drivers_info.h"

#include "kernel_translation_tables.h"

void VBAR_SEL1();

vm_state __attribute__(( aligned( 16 ) )) vm[2] = {
    {
      .cntkctl_el1 = 0b1100000011, // ARM DDI 0487C.a D10-2942
      .mair_el1 = Aarch64_VMSA_Isambard_memory_attributes,
      .sctlr_el1 = 0,     // Set in switch_to_running_in_high_memory
      .tcr_el1 = 0,       // Set in switch_to_running_in_high_memory
      .ttbr1_el1 = 0,     // Set in switch_to_running_in_high_memory
      // .ttbr0_el1 = 0, core-specific
      .vbar_el1 = (uint64_t) VBAR_SEL1
    } };

extern void _start(); // For the PC-relative address of the start of the code

// w18 contains a thread code, locks may contain two thread codes
// Valid thread codes are non-zero.
static inline uint32_t thread_code( thread_context *t )
{
  if (0 == t) return 0;
  return ((uint8_t*) t) - (uint8_t*) _start;
}

static inline thread_context *thread_from_code( uint32_t c )
{
  if (c == 0) return 0;
  return (thread_context*) (((uint8_t*) _start) + c);
}

#include "doubly_linked_lists.h"
DEFINE_DOUBLE_LINKED_LIST( thread, thread_context, next, prev, list );

#define BSOD_TOSTRING( n ) #n
#define BSOD( n ) asm ( "0: smc " BSOD_TOSTRING( n ) "\n\tb 0b" );

static inline
uint32_t __attribute__(( always_inline )) core_number()
{
  uint32_t res;
  asm volatile( "\tMRS %[result], MPIDR_EL1" : [result] "=r" (res) );
  // Check uniprocessor flag and return 0 if true.
  return (res & (1 << 30)) == 0 ? (res & 0xff) : 0;
}

static void invalidate_cache( uint8_t code )
{
  // Document Number: ARM DAI 0527A // Version: 1.0 // Non-Confidential // Page 45 of 535.3.2
  asm (
    // Invalidate Data cache to make the code general purpose.
    // Calculate the cache size first and loop through each set + way.
    "\n\tMSR CSSELR_EL1, %[code]" // 0x0 for L1 Dcache, 0x2 for L2 Dcache.
    "\n\tMRS X4, CCSIDR_EL1" // Read Cache Size ID.
    "\n\tAND X1, X4, #0x7"
    "\n\tADD X1, X1, #0x4"
    "\n\tLDR X3, =0x7FFF"
    "\n\tAND X2, X3, X4, LSR #13"
    "\n\tLDR X3, =0x3FF"
    "\n\tAND X3, X3, X4, LSR #3" // X3 = Cache Associativity Number – 1.
    "\n\tCLZ W4, W3" // X4 = way position in the CISW instruction.
    "\n\tMOV X5, #0" // X5 = way counter way_loop.
    // X1 = Cache Line Size.
    // X2 = Cache Set Number – 1.
    "\nway_loop:"
    "\n\tMOV X6, #0" // X6 = set counter set_loop.
    "\nset_loop:"
    "\n\tLSL X7, X5, X4"
    "\n\tORR X7, %[code], X7"
    // Set way.
    "\n\tLSL X8, X6, X1"
    "\n\tORR X7, X7, X8" // Set set.
    "\n\tDC cisw, X7" // Clean and Invalidate cache line.
    "\n\tADD X6, X6, #1" // Increment set counter.
    "\n\tCMP X6, X2" // Last set reached yet?
    "\n\tBLE set_loop" // If not, iterate set_loop,
    "\n\tADD X5, X5, #1" // else, next way.
    "\n\tCMP X5, X3" // Last way reached yet?
    "\n\tBLE way_loop" // If not, iterate way_loop.
    "\n\tret"
    "\nout:"
    : : [code] "r" (code) : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8"
    );
}

void invalidate_all_caches()
{
  invalidate_cache( 0 );
  invalidate_cache( 2 ); // The GPU does not see changes, without this.
}

const uint32_t max_physical_memory_gb = 8;
const uint64_t max_physical_memory = max_physical_memory_gb * (1ull << 30);
uint64_t memory_allocator_driver_start = max_physical_memory;

// Linker-created symbols are either absolute or relative, *_code_pages,
// *_data_pages are absolute (see build.sh), *_bin_start, *_bin_end are
// relative. Not that it matters, the absolutes are added to the relative
// address of _start, when their address is taken, too.
static uint64_t phys_addr( void *kernel_addr )
{
  uint8_t *start = (uint8_t*) _start;
  return ((uint8_t*) kernel_addr) - start;
}

#define AARCH64_VECTOR_TABLE_NAME VBAR_SEL1
#define AARCH64_VECTOR_TABLE_PREFIX SEL1_
#define HANDLER_EL 1

#include "aarch64_c_vector_table.h"

static Aarch64_VMSA_entry shared_system_map[32] = { Aarch64_VMSA_invalid };
static uint32_t shared_system_map_core_page = 0;
static uint32_t shared_system_map_mapped_pages = 0;

static void initialise_system_map()
{
  integer_register system_code_pages = drivers[0].code_pages;
  integer_register system_data_pages = drivers[0].data_pages;
  integer_register system_code_start = drivers[0].start;
  integer_register system_data_start = system_code_start + (system_code_pages << 12);

  uint32_t index = 0;

  for (integer_register i = 0; i < system_code_pages; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( system_code_start + (i << 12) );
    entry = Aarch64_VMSA_el0_r_x( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    entry.shareability = 0; // Non-shareable, will never change
    entry.not_global = 1;
    entry.access_flag = 1;
    shared_system_map[index++] = entry;
  }

  for (integer_register i = 0; i < system_data_pages; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( system_data_start + (i << 12) );
    entry = Aarch64_VMSA_el0_rw_( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    entry.shareability = 3; // Inner shareable
    entry.not_global = 1;
    entry.access_flag = 1;
    shared_system_map[index++] = entry;
  }

  shared_system_map_core_page = index; // This is fixed, from now on.
  shared_system_map_mapped_pages = index + 1; // This is not, see Isambard_System_Service_Add_Device_Page
}

int no_zero_bsod = __COUNTER__;
int no_one_bsod = __COUNTER__;
int no_two_bsod = __COUNTER__;

// The following variables are offsets into the kernel's working memory, so
// that that memory can be located in low (EL2,3) or high (EL1) virtual memory.
// In both cases, the address of _start will give the base address, for pointers.
static const uint32_t top_of_kernel_working_memory = 2 * 1024 * 1024;

static uint32_t kernel_heap_top = top_of_kernel_working_memory;
static uint32_t kernel_heap_bottom = top_of_kernel_working_memory;
static uint32_t kernel_interfaces_offset = top_of_kernel_working_memory;
static const uint32_t number_of_special_interfaces = 2;
static interface_index kernel_free_interface = 0;
static interface_index kernel_last_interface = number_of_special_interfaces;
extern integer_register volatile number_of_cores;
extern integer_register volatile first_free_page;

static bool could_be_in_heap( uint32_t p )
{
  return (p < kernel_heap_top && p >= kernel_heap_bottom);
}

static void *heap_pointer_from_offset( uint32_t heap_offset )
{
  uint8_t *top = ((uint8_t*) _start) + top_of_kernel_working_memory;
  return top - heap_offset;
}

static void *heap_pointer_from_offset_lsr4( uint32_t heap_offset_lsr4 )
{
  return heap_pointer_from_offset( heap_offset_lsr4 << 4 );
}

static uint32_t heap_offset( void *p )
{
  uint8_t *top = ((uint8_t*) _start) + top_of_kernel_working_memory;
  return (top - (uint8_t*)p);
}

static uint32_t heap_offset_lsr4( void *p )
{
  return heap_offset( p ) >> 4;
}

static void read_heap( uint64_t offset, uint64_t length, void *destination )
{
  if (offset > kernel_heap_top - kernel_heap_bottom) {
    BSOD( __COUNTER__ );
  }

  if (0 != (offset & 15)) { // Heap always aligned on 16-byte boundaries
    BSOD( __COUNTER__ );
  }

  if (0 != (length & 15)) { // Heap always aligned on 16-byte boundaries
    BSOD( __COUNTER__ );
  }

  void *source = (uint8_t*) _start + kernel_heap_top - offset;

  uint64_t *s = source;
  uint64_t *d = destination;
  for (uint32_t i = 0; i < length / sizeof( *d ); i++) {
    d[i] = s[i];
  }
}

static void write_heap( uint64_t offset, uint64_t length, void *source )
{
  if (offset > kernel_heap_top - kernel_heap_bottom) {
    BSOD( __COUNTER__ );
  }

  if (0 != (offset & 15)) { // Heap always aligned on 16-byte boundaries
    BSOD( __COUNTER__ );
  }

  if (0 != (length & 15)) { // Heap always aligned on 16-byte boundaries
    BSOD( __COUNTER__ );
  }

  void *destination = (uint8_t*) _start + kernel_heap_top - offset;

  uint64_t *s = source;
  uint64_t *d = destination;
  for (uint32_t i = 0; i < length / sizeof( *d ); i++) {
    d[i] = s[i];
asm volatile ( "dc ivac, %[va]" : : [va] "r" (&d[i]) ); // Does this fix it, can it be reduced?
  }
}

void *allocate_heap( uint64_t size )
{
  uint32_t new_bottom;
  size = (size + 31) & ~31ull;
  do {
    new_bottom = load_exclusive_word( &kernel_heap_bottom ) - size;
  } while (!store_exclusive_word( &kernel_heap_bottom, new_bottom ));
  return (void*) (((uint8_t*)_start) + new_bottom);
}

static void free_heap( uint64_t offset, uint64_t size )
{
  if (offset > kernel_heap_top - kernel_heap_bottom) {
    BSOD( __COUNTER__ );
  }

  if (0 != (offset & 15)) { // Heap always aligned on 16-byte boundaries
    BSOD( __COUNTER__ );
  }

  if (0 != (size & 15)) {
    BSOD( __COUNTER__ );
  }
  BSOD( __COUNTER__ );
}

static const uint64_t free_marker = 0x00746e4965657246;

static Interface *interfaces()
{
  return (Interface*) (((uint8_t*) _start) + kernel_interfaces_offset);
}

static Interface *interface_from_index( uint32_t index )
{
  if (index > kernel_last_interface) {
    return 0;
  }
  return interfaces() + index;
}

static uint32_t index_from_interface( Interface *interface )
{
  return interface - interfaces();
}

static void free_interface( Interface *i )
{
  i->free.marker = free_marker;
  do {
    do {
      i->free.next = load_exclusive_word( &kernel_free_interface );
    } while (i->free.next == 0);
  } while (!store_exclusive_word( &kernel_free_interface, index_from_interface( i ) ));
}

static void new_memory_for_interfaces( integer_register new_last )
{
  integer_register first_new = kernel_last_interface + 1;
  Interface *ii = interfaces();
  for (integer_register i = first_new; i < new_last; i++) {
    ii[i].free.marker = free_marker;
    ii[i].free.next = i + 1;
  }
  Interface *last_interface = &ii[new_last - 1];
  last_interface->free.next = 0;
  kernel_last_interface = new_last;
  kernel_free_interface = first_new;
}

static Interface *obtain_interface()
{
  Interface *result;
  interface_index head;
  do {
    do {
      head = load_exclusive_word( &kernel_free_interface );
      if (head == 0) { clear_exclusive(); }
    } while (head == 0); // The core that wrote 0 will be allocating more interfaces
    result = interface_from_index( head );
  } while (!store_exclusive_word( &kernel_free_interface, result->free.next ));

  if (result->free.marker != free_marker) {
    for (;;) { BSOD( __COUNTER__ ) }
  }

  if (result->free.next == 0) { // Just emptied the list, allocate some more
    for (;;) { BSOD( __COUNTER__ ) }
  }

  return result;
}

#define numberof( a ) (sizeof( a ) / sizeof( a[0] ))

void initialise_new_thread( thread_context *thread )
{
  for (int i = 0; i < 31; i++) {
    thread->regs[i] = ((0xffffff & (uint64_t) thread) << 8) + i;
  }

  // No longer free, must always be in a linked list
  thread->next = thread;
  thread->prev = thread;
  thread->list = 0;
  thread->partner = 0;
  thread->spsr = 0;
  thread->gate = 0;
  thread->fp = 0;
  thread->regs[18] = thread_code( thread );

  // No particularly good reason for a downward growing stack...
  thread->stack_pointer = thread->stack + numberof( thread->stack ) - 1;
  thread->stack_limit = thread->stack;
  thread->stack_pointer->caller_sp = 0;
  thread->stack_pointer->caller_map = system_map_index;
  thread->stack_pointer->caller_return_address = System_Service_ThreadExit;
}

static void load_system_map( Core *core )
{
  // This is a special map. It is:
  //   Loaded in full before entry, so there will never be a fault when this map is active
  //   Has a core-specific stack and structure
  Aarch64_VMSA_entry volatile *core_tt_l1 = core->core_tt_l1;
  core_tt_l1[0] = Aarch64_VMSA_subtable_at( core->physical_address->core_tt_l2 );
  Aarch64_VMSA_entry volatile *core_tt_l2 = core->core_tt_l2;
  // System driver code running at 0
  core_tt_l2[0] = Aarch64_VMSA_subtable_at( core->physical_address->core_tt_l3 );

  Aarch64_VMSA_entry volatile *core_tt_l3 = core->core_tt_l3;

  for (uint32_t i = 0; i < shared_system_map_mapped_pages; i++) {
    core_tt_l3[i] = shared_system_map[i];
  }

  // The appropriate Core system_thread_stack will appear after the driver's data
  {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( ((uint64_t) &core->physical_address->system_thread_stack) );
    entry = Aarch64_VMSA_el0_rw_( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    entry.shareability = 0; // Non-shareable, core local
    entry.not_global = 1;
    entry.access_flag = 1;
    core_tt_l3[shared_system_map_core_page] = entry;
  }

  // asm volatile ( "isb\n\tdsb sy\n\tTLBI ALLE1\n\tisb\n\tdsb sy" );
  core->loaded_map = system_map_index;
}

static void load_memory_allocator_map( Core *core )
{
  // This is a standard map; there should never be a fault when this map is active
  //
  // VA 0 - 8GB, mapped to physical addresses.
  Aarch64_VMSA_entry volatile *core_tt_l1 = core->core_tt_l1;
  for (uint64_t i = 0; i < max_physical_memory_gb; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( i << 30 );
    entry = Aarch64_VMSA_el0_rw_( entry );
    // It is up to the system code to ensure that there are no TLBs or caches pointing to the memory
    // before relasing it to the physical memory manager.
    entry = Aarch64_VMSA_uncached_memory( entry );
    entry.shareability = 0; // Non-shareable, single core, it's uncached anyway, and the driver never accesses mapped memory
    entry.not_global = 1;
    entry.access_flag = 1;
    core_tt_l1[i] = entry;
  }

  // VA 8GB+ mapped to driver code (to run at el0), via a subtable of 2MB areas
  core_tt_l1[max_physical_memory_gb] = Aarch64_VMSA_subtable_at( core->physical_address->core_tt_l2 );

  Aarch64_VMSA_entry volatile *core_tt_l2 = core->core_tt_l2;
  // The first of which is a subtable of 4k pages...
  core_tt_l2[0] = Aarch64_VMSA_subtable_at( core->physical_address->core_tt_l3 );

  Aarch64_VMSA_entry volatile *core_tt_l3 = core->core_tt_l3;

  integer_register memory_allocator_code_pages = drivers[1].code_pages;
  integer_register memory_allocator_data_pages = drivers[1].data_pages;
  integer_register memory_allocator_code_start = drivers[1].start;
  integer_register memory_allocator_data_start = memory_allocator_code_start + (memory_allocator_code_pages << 12);

  for (integer_register i = 0; i < memory_allocator_code_pages; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( memory_allocator_code_start + (i << 12) );
    entry = Aarch64_VMSA_el0_r_x( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    entry.shareability = 1;
    entry.not_global = 1;
    entry.access_flag = 1;
    core_tt_l3[i] = entry;
  }

  for (integer_register i = 0; i < memory_allocator_data_pages; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( memory_allocator_data_start + (i << 12) );
    entry = Aarch64_VMSA_el0_rw_( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    entry.shareability = 1;
    entry.not_global = 1;
    entry.access_flag = 1;
    core_tt_l3[i + memory_allocator_code_pages] = entry;
  }

  core->loaded_map = memory_allocator_map_index;
}

static inline uint64_t psr_for_map( interface_index new_map )
{
  UNUSED( new_map );
  return 0x0; // FIXME Need L2 maps, as well
}

static void clear_core_translation_tables( Core *core )
{
  // FIXME Remember lowest and highest used entries for quicker cleaning
  // Also, use disable_el1_translation_tables instead, and switch and clear on demand
  for (unsigned i = 0; i < numberof( core->core_tt_l3 ); i++) {
    core->core_tt_l3[i] = Aarch64_VMSA_invalid;
  }
  for (unsigned i = 0; i < numberof( core->core_tt_l2 ); i++) {
    core->core_tt_l2[i] = Aarch64_VMSA_invalid;
  }
  for (unsigned i = 0; i < numberof( core->core_tt_l1 ); i++) {
    core->core_tt_l1[i] = Aarch64_VMSA_invalid;
  }
}

void load_this_map( Core *core, interface_index new_map )
{
  if (core->loaded_map != new_map) {
    clear_core_translation_tables( core );

    if (new_map == memory_allocator_map_index) {
      load_memory_allocator_map( core );
    }
    if (new_map == system_map_index) {
      load_system_map( core );
    }

    // asm volatile ( "isb\n\tdsb sy\n\tTLBI ALLE1\n\tTLBI ALLE3\n\tisb\n\tdsb sy" ); // Needed?

    core->loaded_map = new_map;
  
    set_asid( core, new_map ); // FIXME Will run out of values really soon!
  }
}

void change_map( Core *core, thread_context *thread, interface_index new_map )
{
  // Ensure the index is within range
  if (thread->current_map > kernel_last_interface) {
    BSOD( __COUNTER__ );
  }
  if (new_map > kernel_last_interface) {
    BSOD( __COUNTER__ );
  }

  if (core->loaded_map != new_map) {
    load_this_map( core, new_map );
  }

  if (core->loaded_map != new_map) {
    BSOD( __COUNTER__ );
  }

  thread->current_map = new_map;
}

static void __attribute__(( noinline )) incompatible_event()
{
  BSOD( __COUNTER__ );
}

static inline integer_register fault_address()
{
  integer_register result;
  asm volatile ( "mrs %[result], FAR_EL1" : [result] "=r" (result) );
  return result;
}

static const int level1_lsb = 12 + 9 + 9;
static const int level2_lsb = 12 + 9;
static const int level3_lsb = 12;

static inline Aarch64_VMSA_entry with_virtual_memory_attrs( Aarch64_VMSA_entry entry, VirtualMemoryBlock *vmb )
{
  entry.shareability = 3; // FIXME, not needed when map only on one core?
  entry.not_global = 1;
  entry.access_flag = 1;

  if (vmb->read_only) {
    if (vmb->executable) {
      return Aarch64_VMSA_el0_r_x( entry );
    }
    else {
      return Aarch64_VMSA_el0_ro_( entry );
    }
  }
  else {
    if (vmb->executable) {
      return Aarch64_VMSA_el0_rwx( entry );
    }
    else {
      return Aarch64_VMSA_el0_rw_( entry );
    }
  }

  return entry;
}

static Aarch64_VMSA_entry with_physical_memory_attrs( Aarch64_VMSA_entry entry, ContiguousMemoryBlock cmb )
{
  entry.memory_type = cmb.memory_type;
  return entry;
}

static VirtualMemoryBlock *find_vmb( thread_context *thread, uint64_t fa )
{
  uint64_t fa_page = (fa >> level3_lsb);
  Interface *map = interfaces()+thread->current_map;

  MapValue mv = { .r = map->object.as_number };

  VirtualMemoryBlock *vmb = heap_pointer_from_offset_lsr4( mv.heap_offset_lsr4 );

  if (mv.heap_offset_lsr4 > ((kernel_heap_top - kernel_heap_bottom)>>4)) {
    BSOD( __COUNTER__ );
  }

  while (vmb->page_count > 0) {
    int32_t page_offset = fa_page - vmb->start_page;
    if (page_offset >= 0 && page_offset < vmb->page_count) {
      return vmb;
    }
    vmb++;
  }

  return 0;
}

static bool is_l1_aligned( uint64_t start_page, uint64_t page_count )
{
  return (0 == (start_page & ((1 << 18)-1)))
      && (0 == (page_count & ((1 << 18)-1)));
}

static bool is_l2_aligned( uint64_t start_page, uint64_t page_count )
{
  return (0 == (start_page & ((1 << 9)-1)))
      && (0 == (page_count & ((1 << 9)-1)));
}

static bool find_and_map_memory( Core *core, thread_context *thread, uint64_t fa )
{
  if (thread->current_map == system_map_index) {
    BSOD( __COUNTER__ ); // System map exception
  }
  if (thread->current_map == memory_allocator_map_index) {
    BSOD( __COUNTER__ ); // Memory manager map exception
  }
  VirtualMemoryBlock *vmb = find_vmb( thread, fa );
  if (vmb == 0) {
    asm ( "smc 3" );
    asm ( "mov %0, %0\nwfi" : : "r" (fa) );
    // Throw unnamed exception FIXME
    // i.e. set spsr V flag, and resume? No! Not entered by programmer action!
  }
  else {
    Interface *memory_provider = interface_from_index( vmb->memory_block );

    if (0 == memory_provider)
      BSOD( __COUNTER__ );

    if (memory_provider->provider == system_map_index
     && memory_provider->handler == System_Service_PhysicalMemoryBlock) {
      ContiguousMemoryBlock cmb = { .r = memory_provider->object.as_number };
      uint64_t physical_memory_start = cmb.start_page << 12;
      uint64_t virtual_memory_start = vmb->start_page << 12;

      Aarch64_VMSA_entry entry;
      Aarch64_VMSA_entry volatile *entry_location;
      // FIXME FIXME FIXME Doesn't remove old entries.
      // This section needs a lot of work, it needs to clear only what it needs to,
      // initialise reasonable amounts of translation table, to avoid subsequent exceptions,
      // identify areas of memory that can be marked as contiguous (16 entries, iirc)...
      //  tt_lX_base_address, vmid/map, subtable entry, lowest/highest used entry...

      if (is_l1_aligned( vmb->start_page, vmb->page_count )
       && is_l1_aligned( cmb.start_page, cmb.page_count )) {
        entry = Aarch64_VMSA_block_at( physical_memory_start + ((fa - virtual_memory_start) & (-1ull << level1_lsb)) );
        entry_location = &core->core_tt_l1[(fa >> level1_lsb)&511];
      }
      else {
        Aarch64_VMSA_entry volatile *core_tt_l1 = core->core_tt_l1;
        core_tt_l1[(fa >> level1_lsb)&511] = Aarch64_VMSA_subtable_at( core->physical_address->core_tt_l2 );

        if (is_l2_aligned( vmb->start_page, vmb->page_count )
         && is_l2_aligned( cmb.start_page, cmb.page_count )) {
                // FIXME: Doesn't cater for blocks straddling table boundaries, among other things.
          entry = Aarch64_VMSA_block_at( physical_memory_start + ((fa - virtual_memory_start) & (-1ull << level2_lsb)) );
          entry_location = &core->core_tt_l2[(fa >> level2_lsb)&511];
        }
        else {
          Aarch64_VMSA_entry volatile *core_tt_l2 = core->core_tt_l2;
          core_tt_l2[(fa >> level2_lsb)&511] = Aarch64_VMSA_subtable_at( core->physical_address->core_tt_l3 );

          Aarch64_VMSA_entry volatile *core_tt_l3 = core->core_tt_l3;
          entry = Aarch64_VMSA_page_at( physical_memory_start + ((fa - virtual_memory_start) & (-1ull << level3_lsb)) );
          entry_location = &core_tt_l3[(fa >> level3_lsb)&511];
        }
      }

      entry = with_physical_memory_attrs( entry, cmb );
      entry = with_virtual_memory_attrs( entry, vmb );
      *entry_location = entry;

      return true;
    }
    else {
      BSOD( __COUNTER__ );
      // No non-memory-manager memory areas supported yet.
    }
  }

  return false;
}

extern int at_writable_start;
extern int at_writable_end;

static uint64_t pages_needed_for( uint64_t size )
{
  return ((size + 4095) >> 12);
}

static uint64_t heap_space_needed_for_cores( int cores )
{
        // TODO inter-core communications matrix
  return cores * (sizeof( thread_context ));
}

static uint64_t interfaces_needed_per_core()
{
  return 1; // ?
}

extern void assign_kernel_entry( Aarch64_VMSA_entry entry, uint64_t kernel_page );
extern void *himem_address( void *va );

void map_initial_storage( Core *core0, unsigned initial_heap, unsigned initial_interfaces )
{
  // Map memory for interfaces (grows up), and heap (grows down)

  if (initial_interfaces < number_of_special_interfaces) for (;;) { BSOD( __COUNTER__ ) }

  int heap_pages = pages_needed_for( initial_heap );
  int interface_pages = pages_needed_for( initial_interfaces * sizeof( Interface ) );

  kernel_interfaces_offset = ((uint8_t*) (core0 + number_of_cores) - (uint8_t*)_start);
  int interfaces_page = (kernel_interfaces_offset >> 12);
  integer_register first_physical_interfaces_page = first_free_page;
  first_free_page += (interface_pages << 12);

  for (int i = 0; i < interface_pages; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( first_physical_interfaces_page + (i << 12) );
    entry = Aarch64_VMSA_priv_rw_( entry, 1 );
    entry.access_flag = 1; // Don't want to be notified when accessed
    entry.shareability = 3; // Inner shareable
    entry.not_global = 0;
    entry = Aarch64_VMSA_write_back_memory( entry );

    assign_kernel_entry( entry, interfaces_page+i );
  }

  // Note, this is at the top of 2MB, not 32MB.
  // Changing the kernel maps to 16k granules would mean:
  //  One, 16k, table with 2048 16k pages mapping 32MB
  //  Much more overhead (still pretty minimal, when thinking of gigabytes)
  integer_register first_physical_heap_page = first_free_page;
  first_free_page += (heap_pages << 12);
  for (int i = 0; i < heap_pages; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( first_physical_heap_page + (i << 12) );
    entry = Aarch64_VMSA_priv_rw_( entry, 1 );
    entry.access_flag = 1; // Don't want to be notified when accessed
    entry.shareability = 3; // Inner shareable
    entry.not_global = 0;
    entry = Aarch64_VMSA_write_back_memory( entry );

    assign_kernel_entry( entry, (512-heap_pages+i) );
  }
}

uint64_t volatile standard_isambard_cores = 0;

void initialise_driver_maps( Core *core0, integer_register first_free_page )
{
  // Running in high memory

  { // System driver
    Interface *system_map = interface_from_index( system_map_index );
    system_map->object.as_number = 0x535953;
    system_map->user = system_map_index;
    system_map->provider = system_map_index;
    system_map->handler = System_Service_Map;
  }

  { // Memory driver
    Interface *memory_manager_map = interface_from_index( memory_allocator_map_index );
    memory_manager_map->object.as_number = 0x4d454d;
    memory_manager_map->user = system_map_index;
    memory_manager_map->provider = memory_allocator_map_index;
    memory_manager_map->handler = max_physical_memory; // Code mapped above physical memory
  }

  for (uint8_t n = 0; (1ull << n) <= standard_isambard_cores && (1ull << n) != 0; n++) {
    if (0 != (standard_isambard_cores & (1ull << n))) {
      thread_context *thread = (void*) allocate_heap( sizeof( thread_context ) );
      // System initialisation thread
      initialise_new_thread( thread );
      thread->current_map = system_map_index;
      thread->pc = 0x0;
      thread->spsr = 0x0;
      thread->regs[0] = system_map_index;
      thread->regs[1] = memory_allocator_map_index;
      thread->regs[2] = n;
      thread->regs[3] = first_free_page;
      core0[n].runnable = thread;
    }
  }

  for (unsigned i = number_of_system_maps; i < numberof( drivers ); i++) {
    Interface *map_interface = obtain_interface();
    Interface *code_interface = obtain_interface();
    Interface *data_interface = obtain_interface();

    if ((drivers[i].start & 0xfff)
     || (drivers[i].end & 0xfff)) {
      BSOD( __COUNTER__ );
    }
    static const int INITIAL_VMBs_PER_DRIVER = 12;

    VirtualMemoryBlock *vmb = (void*) allocate_heap( INITIAL_VMBs_PER_DRIVER * sizeof( VirtualMemoryBlock ) );

    MapValue mv = { .heap_offset_lsr4 = heap_offset_lsr4( vmb ),
        .map_object = index_from_interface( map_interface ),
        .number_of_vmbs = INITIAL_VMBs_PER_DRIVER };

    map_interface->object.as_number = mv.r;
    map_interface->user = index_from_interface( map_interface );
    map_interface->provider = system_map_index;
    map_interface->handler = System_Service_Map;

    vmb[0].start_page = 0;
    vmb[0].page_count = drivers[i].code_pages;
    vmb[0].read_only = 1;
    vmb[0].executable = 1;
    vmb[0].memory_block = index_from_interface( code_interface );

    vmb[1].start_page = drivers[i].code_pages;
    vmb[1].page_count = drivers[i].data_pages;
    vmb[1].read_only = 0;
    vmb[1].executable = 0;
    vmb[1].memory_block = index_from_interface( data_interface );

    vmb[2].r = 0;

    { ContiguousMemoryBlock cmb = { .start_page = (uint32_t) drivers[i].start >> 12, .page_count = drivers[i].code_pages, .memory_type = Fully_Cacheable };
      code_interface->object.as_number = cmb.r;
      code_interface->user = index_from_interface( map_interface );
      code_interface->provider = system_map_index;
      code_interface->handler = System_Service_PhysicalMemoryBlock;
    }

    { ContiguousMemoryBlock cmb = { .start_page = ((uint32_t) drivers[i].start >> 12) + drivers[i].code_pages, .page_count = drivers[i].data_pages, .memory_type = Fully_Cacheable };
      data_interface->object.as_number = cmb.r;
      data_interface->user = index_from_interface( map_interface );
      data_interface->provider = system_map_index;
      data_interface->handler = System_Service_PhysicalMemoryBlock;
    }

    thread_context *thread = (void*) allocate_heap( sizeof( thread_context ) );

    initialise_new_thread( thread );

    thread->current_map = index_from_interface( map_interface );
    thread->pc = 0x0;
    thread->spsr = 0x0;
    thread->regs[0] = index_from_interface( map_interface );

    insert_thread_at_tail( &core0->runnable, thread );
  }
}

void __attribute__(( noreturn )) enter_secure_el1_himem( Core *core )
{
  static volatile bool initialising = true;

  asm volatile ( "\tmsr VBAR_EL1, %[table]\n" : : [table] "r" (VBAR_SEL1) );

  vm[0].vbar_el1 = (uint64_t) VBAR_SEL1;

  asm volatile ( "\tmsr CNTKCTL_EL1, %[bits]\n" : : [bits] "r" (vm[0].cntkctl_el1) );

  core->core = core;

  if (core->core_number == 0) {
    static const int total_heap_space_needed = 65536; // FIXME
    static const int total_number_of_interfaces_needed = 512; // FIXME

    map_initial_storage( core, total_heap_space_needed, total_number_of_interfaces_needed );

    new_memory_for_interfaces( total_number_of_interfaces_needed );

    initialise_system_map();

    // FIXME real start and end
    initialise_driver_maps( core, first_free_page );

    initialising = false;

    asm volatile ( "dsb sy" );

    asm volatile ( "sev" );
  }
  else {
    while (initialising) { asm( "dsb sy\nwfe" ); }
    while (core->runnable == 0) { asm( "dsb sy\nwfe" ); }
  }

  core->loaded_map = illegal_interface_index;
  //asm volatile ( "mov %0, %0\n\tmov %1, %1\n\tmov %2, %2\n\twfi" : : "r" (core), "r" (core->runnable), "r" (core->runnable->current_map) );
  load_this_map( core, core->runnable->current_map );

  thread_switch threads = { .now = core->runnable, .then = 0 }; 

  asm volatile ( "mov sp, %[core_stack_top]" : : [core_stack_top] "r" (&core->core) );

  el1_enter_thread( threads );

  __builtin_unreachable();
}

extern void switch_to_running_in_high_memory( Core *phys_core, void (*himem_code)( Core *core ) );

void __attribute__(( noreturn )) isambard_secure_el1( Core *phys_core, int number, uint64_t *present )
{
  static volatile bool initialising = true;

  standard_isambard_cores = *present; // Good to 64 cores, extend to an array

  // This is entered in low memory, with caches disabled.

  clear_core_translation_tables( phys_core );

  phys_core->core_number = number;

  if (number == 0) {
    initialise_shared_isambard_kernel_tables( phys_core, number_of_cores );

    initialising = false;

    asm volatile ( "dsb sy" );

    asm volatile ( "sev" );
  }
  else {
    while (initialising) { asm( "dsb sy\nwfe" ); }
  }

  switch_to_running_in_high_memory( phys_core, himem_address( enter_secure_el1_himem ) );

  __builtin_unreachable();
}

static thread_switch system_driver_request( Core *core, thread_context *thread )
{
  // Note: The system driver is responsible for ensuring that this is only called for one core at a time.
  thread_switch result = { .now = thread, .then = thread };

  switch (thread->regs[0]) {

  case Isambard_System_Service_Add_Device_Page:
    {
      // This will only affect the calling core's loaded memory map, use Isambard_System_Service_Updated_Map
      Aarch64_VMSA_entry volatile *core_tt_l3 = core->core_tt_l3;

      Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( thread->regs[1] );
      entry = Aarch64_VMSA_device_memory( entry );
      entry = Aarch64_VMSA_el0_rw_( entry );

      entry.shareability = 3; // Inner shareable
      entry.not_global = 1;
      entry.access_flag = 1;

      uint32_t page = thread->regs[2] >> 12;

      if (page >= numberof( shared_system_map )) {
        for (;;) BSOD( __COUNTER__ )
      }

      if (shared_system_map[page].raw != Aarch64_VMSA_invalid.raw) {
        for (;;) BSOD( __COUNTER__ )
      }
      core->system_thread_stack.entry[4] = thread->regs[2];

      shared_system_map[page] = entry;
      core_tt_l3       [page] = entry;

      if (page >= shared_system_map_mapped_pages) {
        shared_system_map_mapped_pages = page + 1;
      }

      thread->regs[0] = (page << 12);
    }
    break;
  case Isambard_System_Service_Updated_Map:
    {
      Aarch64_VMSA_entry volatile *core_tt_l3 = core->core_tt_l3;

      // Make it instantly usable, not just next time the system map is loaded.
      // The implicit DSB from the eret should ensure the memory isn't accessed before it's written to memory.
      for (uint32_t i = shared_system_map_core_page + 1; i < shared_system_map_mapped_pages; i++) {
        core_tt_l3[i] = shared_system_map[i];
      }
    }
    break;

  case Isambard_System_Service_CreateMap: // Code pb, Code va, Data pb, Data pb,
    {
      Interface *e = obtain_interface();

      e->user = thread->stack_pointer[0].caller_map;
      e->provider = thread->current_map;
      e->handler = thread->regs[0];
      e->object.as_number = thread->regs[1];

      thread->regs[0] = index_from_interface( e );
      return result;
    }
    {
    static volatile uint32_t driver_index = 0;
    uint32_t driver;
    do {
      driver = load_exclusive_word( &driver_index );
    } while (!store_exclusive_word( &driver_index, driver+1 ));
    if (driver < numberof( drivers )) {
      thread->regs[0] = drivers[driver].start;
      thread->regs[1] = drivers[driver].code_pages;
      thread->regs[2] = drivers[driver].data_pages;
      uint32_t end = drivers[driver].start + ((drivers[driver].code_pages + drivers[driver].data_pages) << 12);
      if (drivers[driver].end != end)
      {
        BSOD( __COUNTER__ );
      }
    }
    else {
      thread->regs[0] = 0;
    }
    }
    break;
  case Isambard_System_Service_ReadInterface:
    // FIXME Check the provider, user, and, possibly, handler match the system driver's expectations
    {
    Interface *e = interface_from_index( thread->regs[1] );
    if (e != 0)
      thread->regs[0] = e->object.as_number;
    else
      BSOD( __COUNTER__ );
    }
    break;
  case Isambard_System_Service_ReadHeap:
    read_heap( thread->regs[1], thread->regs[2], (void*) thread->regs[3] );
    break;
  case Isambard_System_Service_WriteHeap:
    write_heap( thread->regs[1], thread->regs[2], (void*) thread->regs[3] );
    break;
  case Isambard_System_Service_AllocateHeap:
    thread->regs[0] = (uint64_t) allocate_heap( thread->regs[1] );
    break;
  case Isambard_System_Service_FreeHeap:
    free_heap( thread->regs[1], thread->regs[2] );
    break;
  case Isambard_System_Service_Create_Thread:
    {
      if (0 != (thread->regs[2] & 0xf)) {
        BSOD( __COUNTER__ ); // FIXME
      }
      thread_context *new_thread = allocate_heap( sizeof( thread_context ) );
      initialise_new_thread( new_thread );
      new_thread->current_map = thread->stack_pointer[0].caller_map;
      new_thread->pc = thread->regs[1];
      new_thread->sp = thread->regs[2];
      new_thread->spsr = 0;
      thread->regs[0] = thread_code( new_thread );
      result.now = new_thread;
      // Run new thread until blocks, then old thread resumes.
      insert_thread_as_head( &core->runnable, result.now );
    }
    break;
  case Isambard_System_Service_Set_Interrupt_Thread:
    if (core->interrupt_thread != 0) {
      if (core->interrupt_thread != thread) {
        BSOD( __COUNTER__ );
      }
    }
    else {
      core->interrupt_thread = thread;
      thread->spsr = 0x80; // IRQs disabled (FIQs stay enabled)
    }
    result.now = thread->next;
    remove_thread( thread );
    core->runnable = result.now;
    break;
  case Isambard_System_Service_Thread_Make_Partner:
    {
      if (vm[1].vttbr_el2 != 0) BSOD( __COUNTER__ ); // Only one VM atm
      if (thread->regs[2] != 4096) BSOD( __COUNTER__ ); // Only one VM atm

vm[1].hcr_el2 = 0x00000003ull; // 32-bit EL0&1, VM, no nesting
vm[1].hstr_el2 = 0xffff;       // Hypervisor System Trap Register
vm[1].vmpidr_el2 = 0xc0000000; // Virtualization Multiprocessor ID Register
vm[1].vpidr_el2 = 0x410fc075;  // Pi2, according to qemu
vm[1].sctlr_el1 = 0xd50070;    //  ditto, except clearing SP alignment check bit
vm[1].vtcr_el2 = 0x800080f22;  // t0sz = 34 (1GB); SL0 = 0, IRGN0, ORGN0 = 0, TG0 = 0 (4k), PS = 0 (4GB), VS=1 (16-bit)

      vm[1].vttbr_el2 = (1ull << 48) | thread->regs[1];

      thread_context *partner = allocate_heap( sizeof( thread_context ) );
      thread->partner = partner;
      initialise_new_thread( thread->partner );
      partner->partner = thread;
      asm ( "dc civac, %[va]" : : [va] "r" (&partner->partner) );
      asm ( "dc civac, %[va]" : : [va] "r" (&thread->partner) );
      // This is the secure map, it is the only one that is allowed to switch
      // between partners.
      partner->current_map = thread->stack_pointer[0].caller_map;
      partner->pc = 0;
      partner->spsr = 0x1d3;
    }
    break;
  default:
    core->system_thread_stack.entry[3] = thread->regs[0];
    BSOD( __COUNTER__ );
    for (;;) {}
  }

  if (result.now != thread && result.now->current_map != thread->current_map) {
    change_map( core, result.now, result.now->current_map );
  }

  return result;
}

// Add thread to a "needs stack" list, and resume system thread, if necessary.
static inline thread_context *thread_stack_is_full( thread_context *thread )
{
        thread = thread;
  BSOD( __COUNTER__ );
  return 0;
}

// Event handlers
thread_switch __attribute__(( noinline )) SEL1_SP0_IRQ_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  incompatible_event();
  return result;
}

thread_switch __attribute__(( noinline )) SEL1_SP0_SYNC_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  //incompatible_event();
  return result;
}

thread_switch __attribute__(( noinline )) SEL1_SPX_SYNC_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  incompatible_event();
  return result;
}

thread_switch __attribute__(( noinline )) SEL1_SPX_IRQ_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  incompatible_event();
  return result;
}

static bool find_and_map_memory( Core *core, thread_context *thread, uint64_t fa );

static bool address_is_user_writable( Core *core, thread_context *thread, uint64_t address )
{
  uint64_t pa;
  asm volatile ( "\tAT S1E0W, %[va]"
               "\n\tmrs %[pa], PAR_EL1"
                 : [pa] "=r" (pa)
                 : [va] "r" (address) );
  if (0 == (pa & 1)) return true;

  if (find_and_map_memory( core, thread, address )) {
    asm volatile ( "  dsb sy"
                 "\n  AT S1E0W, %[va]"
                 "\n  mrs %[pa], PAR_EL1"
                   : [pa] "=r" (pa)
                   : [va] "r" (address) );
    return (0 == (pa & 1));
  }

  return false;
}

static bool is_real_thread( uint32_t code )
{
  if (!could_be_in_heap( code )
   || !could_be_in_heap( code + sizeof( thread_context ) - 1 )) {
    return false;
  }

  thread_context *thread = thread_from_code( code );
  if (thread->next->prev != thread || thread->prev->next != thread) {
    return false;
  }

  return true;
}

void *memcpy(void *dest, const void *src, long unsigned int n)
{
  uint8_t const *s = src;
  uint8_t *d = dest;
  while (n-- > 0) *d++ = *s++;
  return dest;
}

#include "svc_handling.h"

static inline thread_switch SEL1_LOWER_AARCH64_SYNC_CODE_may_change_map( Core *core, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread }; // By default, stay with the same thread
  uint32_t esr;
  asm volatile ( "mrs %[esr], esr_el1" : [esr] "=r" (esr) );

  if (0x5600 == (esr >> 16)) {
    // SVC
    return handle_svc( core, thread, esr & 0xffff );
  }
  else {
    switch (esr >> 26) { // D7-2254 ARM DDI 0487B.a
    case 0b100000: // Instruction Abort from a lower Exception level.
      {
        if (!find_and_map_memory( core, thread, fault_address() )) { // BSOD( __COUNTER__ ); }
asm ( "mrs x20, elr_el1" );
asm ( "mrs x21, far_el1" );
asm ( "mrs x22, esr_el1" );
asm ( "mov x23, #0x20" );
        BSOD( __COUNTER__ ); // Instruction
}
      return result;
      }
    case 0b100100: // Data Abort from a lower Exception level.
      {
        if (!find_and_map_memory( core, thread, fault_address() )) { // BSOD( __COUNTER__ ); }
asm ( "mrs x20, elr_el1" );
asm ( "mrs x21, far_el1" );
asm ( "mrs x22, esr_el1" );
asm ( "mrs x25, ttbr0_el1" );
asm ( "mov x26, #0x24" );
        BSOD( __COUNTER__ ); // Data
}

        return result;
      }
    case 0b100110:
      {
        BSOD( 3 ); // SP alignment fault
        BSOD( __COUNTER__ ); // SP alignment fault
      }
      break;
    case 0b011000:
      {
        BSOD( __COUNTER__ ); // Register access at EL0, e.g. CNTP_TVAL_EL0
      }
      break;
    case 0b100010:
      {
asm ( "mov x20, %[r]" : : [r] "r" (thread->regs[0]) );
asm ( "mov x21, %[r]" : : [r] "r" (thread->regs[1]) );
asm ( "mov x22, %[r]" : : [r] "r" (thread->regs[2]) );
asm ( "mov x23, %[r]" : : [r] "r" (thread->regs[3]) );
asm ( "mov x24, %[r]" : : [r] "r" (thread->regs[4]) );
asm ( "mov x25, %[r]" : : [r] "r" (thread->regs[5]) );
asm ( "mov x26, %[r]" : : [r] "r" (thread->regs[30]) );
        BSOD( __COUNTER__ ); // Misaligned PC
      }
      break;
    case 0b111100: // BRK in Aarch64
      {
        // BRK instruction
        switch (esr & 7) {
        case 0: BSOD( 5 ); break;
        case 1: BSOD( 6 ); break;
        case 2: BSOD( 7 ); break;
        case 3: BSOD( 8 ); break;
        case 4: BSOD( 9 ); break;
        case 5: BSOD( 10 ); break;
        case 6: BSOD( 11 ); break;
        case 7: BSOD( 12 ); break;
        }
      }
      break;
    default:
      {
        // switch ((esr >> 29) & 0x7) { // Top bits
        switch ((esr >> 26) & 0x7) { // Lower 3 bits
        case 0: BSOD( 5 ); break;
        case 1: BSOD( 6 ); break;
        case 2: BSOD( 7 ); break;
        case 3: BSOD( 8 ); break;
        case 4: BSOD( 9 ); break;
        case 5: BSOD( 10 ); break;
        case 6: BSOD( 11 ); break;
        case 7: BSOD( 12 ); break;
        }
        BSOD( __COUNTER__ ); // Unknown ESR
      }
    };
  }
  // Blue screen of death time...
  if (1) BSOD( __COUNTER__ );

  return result;
}

thread_switch __attribute__(( noinline )) SEL1_LOWER_AARCH64_SYNC_CODE( void *opaque, thread_context *thread )
{
  Core *core = opaque;
  thread_switch result = SEL1_LOWER_AARCH64_SYNC_CODE_may_change_map( core, thread );
  if (result.now->current_map != result.then->current_map) {
    change_map( core, result.now, result.now->current_map );
  }
  if (core->runnable != result.now) {
    asm ( "smc 0x4444" ); // FIXME, can't I just set core->runnable here?
  }
  return result;
}

thread_switch __attribute__(( noinline )) SEL1_LOWER_AARCH64_IRQ_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  Core *core = opaque;

  result.now = core->interrupt_thread;
  if (0 == core->interrupt_thread) BSOD( __COUNTER__ );

  insert_thread_as_head( &core->runnable, result.now );
  if (result.now->current_map != thread->current_map) {
    change_map( core, result.now, result.now->current_map );
  }

  if (core->runnable != result.now) {
    asm ( "smc 0x3333" ); // FIXME, can't I just set core->runnable here?
  }
  return result;
}

thread_switch __attribute__(( noinline )) SEL1_LOWER_AARCH64_SERROR_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  incompatible_event();
  return result;
}
thread_switch __attribute__(( noinline )) SEL1_LOWER_AARCH32_SYNC_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  incompatible_event();
  return result;
}
thread_switch __attribute__(( noinline )) SEL1_LOWER_AARCH32_IRQ_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  incompatible_event();
  return result;
}
thread_switch __attribute__(( noinline )) SEL1_LOWER_AARCH32_SERROR_CODE( void *opaque, thread_context *thread )
{
  thread_switch result = { .then = thread, .now = thread };
  opaque = opaque;
  incompatible_event();
  return result;
}

