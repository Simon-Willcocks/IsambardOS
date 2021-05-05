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

  // Virtual machine
  thread_context *partner;

  // Do not modify the order of these without good reason!
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
  uint32_t core_number;
  interface_index loaded_map;
  FPContext *fp; // Null if no thread using FP (including thread ending when holding fp)
  thread_context *finished_threads;     // Store of threads that have completed
  thread_context *interrupt_thread;     // Thread that calls interrupt handlers (with interrupts disabled)
  thread_context *blocked_with_timeout;
  struct isambard_core *physical_address;      // Physical address of this struct
  struct isambard_core *low_virtual_address;       // Virtual address of this struct, offset from _start
  // Virtual machine
  struct {
    uint64_t data[8];
  } __attribute__(( aligned( 16 ) )) el2_stack;
  struct {
    uint64_t data[8];
  } __attribute__(( aligned( 16 ) )) el3_stack;
  // Keep the following at the top of a page (CORE_STACK_SIZE is calculated by build.sh)
  uint64_t __attribute__(( aligned( 16 ) )) stack[CORE_STACK_SIZE];
  struct isambard_core *core; // Pointer to the start of this structure
  thread_context *runnable; // Never null - at least the idle thread will be in this list
};

