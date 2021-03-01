/* Copyright (c) 2020 Simon Willcocks */

#include "types.h"

typedef uint32_t interface_index;

typedef struct FPContext FPContext;

typedef struct {
  integer_register caller_sp;
  integer_register caller_return_address;
  uint32_t caller_map;
} inter_map_call_stack_element;

typedef struct thread_context thread_context;

struct thread_context {
  thread_context *next;
  thread_context *prev;
  thread_context **list;
  thread_context *partner;

  integer_register regs[31];
  integer_register sp;
  integer_register pc;
  uint32_t spsr;
  int32_t gate; // 0 no events yet, 1 event already occurred, -1 timed out?

  interface_index current_core;

  interface_index current_map;
  struct FPContext *fp; // Null if thread not using FP

  inter_map_call_stack_element *stack_pointer;
  inter_map_call_stack_element *stack_limit;
  inter_map_call_stack_element stack[6]; // Replace with variable size, shortly.
};

typedef union Interface {
  struct __attribute__(( packed )) {
    interface_index user;
    interface_index provider;
    integer_register handler;
    union {
      void *as_pointer;
      integer_register  as_number;
    } object;
  }; // Anon
  struct __attribute__(( packed )) {
    interface_index next;
    uint64_t     marker; // FreeInt\0 = 0x00746e4965657246
  } free;
} Interface;

static const uint32_t illegal_interface_index = 0;

#include "aarch64_vmsa.h"

struct isambard_core {
  struct {
    uint64_t entry[512];
  } system_thread_stack;
  Aarch64_VMSA_entry core_tt_l3[512]; // 4k pages
  Aarch64_VMSA_entry core_tt_l2[512]; // 2M blocks or level 3 table
  Aarch64_VMSA_entry core_tt_l1[16];  // 1G level 2 tables
  enum { EL3, SECURE_EL1, EL2 } core_state;
  uint32_t core_number;
  interface_index loaded_map;
  FPContext *fp; // Null if no thread using FP (including thread ending when holding fp)
  thread_context *finished_threads;     // Store of threads that have completed
  thread_context *interrupt_thread;     // Thread that calls interrupt handlers (with interrupts disabled)
  struct isambard_core *physical_address;      // Physical address of this struct
  struct isambard_core *low_virtual_address;       // Virtual address of this struct, offset from _start
  // Keep the following at the top of a page (CORE_STACK_SIZE is calculated by build.sh)
  uint64_t __attribute__(( aligned( 16 ) )) stack[CORE_STACK_SIZE];
  struct isambard_core *core; // Pointer to the start of this structure
  thread_context *runnable; // Never null - at least the idle thread will be in this list
};



// Packed objects
typedef union {
  uint64_t r;
  struct __attribute__(( packed )) {
    uint64_t start_page:24; // Max 16GB memory
    uint64_t page_count:20;  // Max 4GB memory in one block
    uint64_t reserved:17;
    uint64_t memory_type:3;  // index into MAIR
  };
} ContiguousMemoryBlock;

typedef union {
  uint64_t r;
  struct __attribute__(( packed )) {
    uint64_t start_page:24; // Max 16GB memory
    uint64_t page_count:20;  // Max 4GB memory in one block
    uint64_t read_only:1;    // combined with physical permissions
    uint64_t executable:1;   //  ditto
    uint64_t memory_block:18; // interface index
  };
} VirtualMemoryBlock;

typedef union {
  uint64_t r;
  struct __attribute__(( packed )) {
    uint64_t heap_offset_lsr4:32;
    uint64_t map_object:20;
    uint64_t number_of_vmbs:12;
  };
} MapValue;

static const interface_index system_map_index = 1;
static const interface_index memory_allocator_map_index = 2;
static const unsigned number_of_system_maps = 2;
