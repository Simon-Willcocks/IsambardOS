/* Copyright 2022 Simon Willcocks
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This is a portion of the RISC OS C kernel I've been working on, intended
 * to allow testing of the virtualisation code under QEMU.
 *
 * It's simplified, and expects to be loaded into RAM at 0.
 *
 * QEMU needs to have some small modifications made to it, see ../../QEMU
 */

#include "kernel.h"

char const build_time[] = "C kernel built: " __DATE__ " " __TIME__ ;

startup boot_data = { 0 };

typedef struct shared_workspace {
  uint64_t tbd[512];
} shared_workspace;

typedef struct core_workspace {
  uint64_t core_number;
  uint32_t counter;
  uint32_t irq_counter;
  struct {
    // Ordering is essential
    uint64_t irq_stack[64];
    uint64_t svc_stack[511]; // Must be final element in structure
  } kernel;
} core_workspace;

// Simple synchronisation routines to be used before the MMU is
// enabled. They make use of the fact that cores may atomically update
// a word in memory. Each core may write to its own element of the
// array, and read the other elements'.
// They require write access to the "ROM", so only work before the MMU
// is initialised.

enum { CORES_AT_BOOT_START = 1, CORES_RUNNING_AT_NEW_LOCATION };

static inline void at_checkpoint( volatile uint32_t *states, int core, int checkpoint )
{
  states[core] = checkpoint;
  while (states[0] != checkpoint) {}
}

static inline void release_from_checkpoint( volatile uint32_t *states, int checkpoint )
{
  states[0] = checkpoint;
}

static inline void wait_for_cores_to_reach( volatile uint32_t *states, int max_cores, int checkpoint )
{
  int done;
  do {
    done = 1;
    for (int i = 1; i < max_cores; i++) {
      if (states[i] != checkpoint) {
        done = 0;
      }
    }
  } while (!done);
}

// Minimum RAM, to start with. More can be added to pool later, if available.
extern const uint32_t minimum_ram;
static const uint32_t top_of_ram = (uint32_t) &minimum_ram;
extern int rom_size;
static const uint32_t size_of_rom = (uint32_t) &rom_size; // 5 << 20;

uint32_t pre_mmu_allocate_physical_memory( uint32_t size, uint32_t alignment, volatile startup *startup );

core_workspace *workspace = 0; // Without enabling the MMU, globals and static locals are writable
// Single core only, atm

static void __attribute__(( noinline )) c_irq()
{
  register uint32_t interrupts asm( "r8" ) = ++workspace->irq_counter;
  register uint32_t loops asm( "r7" ) = workspace->counter;

  asm ( "hvc 14" : "=r" (loops), "=r" (interrupts) : "r" (loops), "r" (interrupts) );
}

void __attribute__(( noinline, noreturn )) pre_mmu_core_with_stack( core_workspace *ws )
{
  workspace = ws;

  // svc_stack follows irq_stack
  asm ( "msr sp_irq, %[stack]" : : [stack] "r" (&ws->kernel.svc_stack) );
  asm ( "cpsie i" ); // Enable interrupts

  // Known to riscos driver
  uint32_t volatile *counter = (void*) 0x400000;

  for (;;) {
    // Spend maybe half the time in the "guest OS" (this code)
    do {
      ++*counter;
    } while (0 != (*counter & 0xfffff));
    // Switch to the hypervisor for the other half
    register uint32_t count asm( "r7" ) = *counter;
    register uint32_t interrupts asm( "r8" ) = workspace->irq_counter;
    asm ( "hvc 15" : : "r" (count), "r" (interrupts) );
  }

  __builtin_unreachable();
}

void _start();

void __attribute__(( noreturn )) locate_rom_and_enter_kernel( uint32_t start, uint32_t core_number, uint32_t volatile *states )
{
  volatile startup *startup = (void*) (((uint8_t*) &boot_data) - ((uint8_t*) _start) + start);

  uint32_t max_cores = 0;

  if (core_number == 0) {
    startup->ram_blocks[0].base = size_of_rom;
    startup->ram_blocks[0].size = top_of_ram - startup->ram_blocks[0].base;

    // Identify the kind of processor we're working with.
    // The overall system (onna chip) will be established later.
    max_cores = pre_mmu_identify_processor();
    for (uint32_t i = 0; i < max_cores; i++) {
      states[i] = 0;
    }

    // Free other cores to indicate they're at CORES_AT_BOOT_START
    startup->states_initialised = true;

    wait_for_cores_to_reach( states, max_cores, CORES_AT_BOOT_START );

    // Other cores are blocked, waiting for the old location of states[0] to change)
    // Release them before starting to work with the potentially new location
    release_from_checkpoint( states, CORES_AT_BOOT_START );

    // Now, we all rush to enter the potentially relocated code
  }
  else {
    while (!startup->states_initialised) {}
    at_checkpoint( states, core_number, CORES_AT_BOOT_START );
  }

  uint32_t const core_workspace_space = (sizeof( core_workspace ) + 0xfff) & ~0xfff;

  if (core_number == 0) {
    uint32_t space_needed = (core_workspace_space * max_cores);

    while (space_needed >= startup->ram_blocks[0].size) { asm( "wfi" ); } // This is never going to happen

    startup->core_workspaces = pre_mmu_allocate_physical_memory( space_needed, 4096, startup );

    startup->shared_memory = pre_mmu_allocate_physical_memory( sizeof( shared_workspace ), 4096, startup );

    {
      uint32_t *p = (void*) startup->shared_memory;
      for (int i = 0; i < sizeof( shared_workspace ) / sizeof( *p ); i++) {
        p[i] = 0;
      }
    }

    wait_for_cores_to_reach( states, max_cores, CORES_RUNNING_AT_NEW_LOCATION );

    // Now all cores are at the new location, so the RAM outside the "ROM" area can be used
    release_from_checkpoint( states, CORES_RUNNING_AT_NEW_LOCATION );
  }
  else {
    at_checkpoint( states, core_number, CORES_RUNNING_AT_NEW_LOCATION );
  }

  core_workspace *ws = (void*) (startup->core_workspaces + core_number * core_workspace_space);

  asm ( "  mov sp, %[stack]" : : [stack] "r" ((&ws->kernel)+1) );

  // Clear out workspace (in parallel)
  uint64_t *p = (void *) ws;
  while (p < &ws->kernel.svc_stack[0]) { // Don't clobber the stack, even if it's not used yet.
    *p++ = 0;
  }
  ws->core_number = core_number;

  pre_mmu_core_with_stack( ws );

  __builtin_unreachable();
}


void __attribute__(( naked )) irq()
{
  asm( "sub lr, lr, #4"
   "\n  srsdb sp!, #0x12"
   "\n  stmfd sp!, {r0-r3, r9, r12}" );

  c_irq();

  asm( "ldmfd sp!, {r0-r3, r9, r12}"
   "\n  rfeia sp!" );
}

// The whole point of this routine is to be linked at the start of the execuable, and
// to pass the actual location of the first byte of the loaded "ROM" to the next
// routine.
void __attribute__(( naked, section( ".text.init" ), noinline )) _start()
{
  register uint32_t start asm( "r0" );

  asm ( "b 0f"
    "\n  hvc #1"
    "\n  hvc #2"
    "\n  hvc #3"
    "\n  hvc #4"
    "\n  hvc #5"
    "\n  b irq"
    "\n  hvc #7"
    "\n0:" );

  asm ( "adr %[loc], _start" : [loc] "=r" (start) ); // Guaranteed PC relative

  uint32_t core_number = get_core_number();

  // Assumes top_of_ram > 2 * size_of_rom and that the ROM
  // is loaded near the top or bottom of RAM.
  uint32_t volatile *states = (uint32_t*) (top_of_ram / 2);
  uint32_t const tiny_stack_size = 256;

  // Allocate a tiny stack per core in RAM that is currently unused.
  asm volatile( "mov sp, %[stack]" : : [stack] "r" (states - core_number * tiny_stack_size) );

  locate_rom_and_enter_kernel( start, core_number, states );
}

// Currently only copes with two alignments/sizes, probably good enough...
static uint32_t allocate_physical_memory( uint32_t size, uint32_t alignment, ram_block *block )
{
  uint32_t result = 1;
  if (block->size >= size && 0 == (block->base & (alignment - 1))) {
    result = block->base;
    block->size -= size;
    block->base += size;
  }
  return result;
}

uint32_t pre_mmu_allocate_physical_memory( uint32_t size, uint32_t alignment, volatile startup *startup )
{
  // Always allocate a least one full page.
  if (0 != (size & 0xfff)) size = (size + 0xfff) & ~0xfff;

  if (startup->less_aligned.size == 0 && 0 != (startup->ram_blocks[0].base & (alignment - 1))) {
    uint32_t misalignment = alignment - (startup->ram_blocks[0].base & (alignment - 1));
    startup->less_aligned.base = startup->ram_blocks[0].base;
    startup->less_aligned.size = misalignment;
    startup->ram_blocks[0].size -= misalignment;
    startup->ram_blocks[0].base += misalignment;
  }

  uint32_t result = allocate_physical_memory( size, alignment, (ram_block*) &startup->less_aligned );
  int block = 0;

  while (0 != (result & 1) && 0 != startup->ram_blocks[block].size) {
    result = allocate_physical_memory( size, alignment, (ram_block*) &startup->ram_blocks[block++] );
  }

  return result;
}

void BOOT_finished_allocating( uint32_t core, volatile startup *startup )
{
  startup->core_entered_mmu = core;
}


