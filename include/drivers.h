#include "types.h"

#ifdef __aarch64__
register uint32_t this_thread asm("x18");
#endif

void entry();

typedef uint32_t Object;

#if 0
// Taken from newlib/libc/machine/aarch64/setjmp.S
// Instead of returning 0/non-0, a longjmp will resume
// at the return exception svc, the routine will return
// to the return svc.

#define GPR_LAYOUT                      \
        REG_PAIR (x19, x20,  0);        \
        REG_PAIR (x21, x22, 16);        \
        REG_PAIR (x23, x24, 32);        \
        REG_PAIR (x25, x26, 48);        \
        REG_PAIR (x27, x28, 64);        \
        REG_PAIR (x29, x30, 80);        \
        REG_ONE (x16,      96)

#define FPR_LAYOUT                      \
        REG_PAIR ( d8,  d9, 112);       \
        REG_PAIR (d10, d11, 128);       \
        REG_PAIR (d12, d13, 144);       \
        REG_PAIR (d14, d15, 160);

#define EXCEPTION_HANDLING_ENTRY( fn ) \
   asm ( \
        #fn"_veneer:" \
   "\n\tadr     x30, 0f" \
   "\n\tmov     x16, sp" \
#define REG_PAIR(REG1, REG2, OFFS)      "\n\tstp " #REG1 ", " #REG2 ", [x0, " #OFFS "]" \
#define REG_ONE(REG1, OFFS)             "\n\tstr " #REG1 ", [x0, " #OFFS "]" \
        GPR_LAYOUT \
        FPR_LAYOUT \
#undef REG_PAIR \
#undef REG_ONE \
   "\n\tbl " #fn \
   "\n\tsvc 0xfffd" \
 "\n0:\tsvc 5" \
   "\n\tb 0b" \
    );
#endif

#ifndef STACK_SIZE
#define STACK_SIZE 64
#endif
#define STACK_SIZE_STRINGX(s) #s
#define STACK_SIZE_STRING(s) STACK_SIZE_STRINGX(s)

extern unsigned long long stack_lock;
extern unsigned long long system; // Initialised by _start code
extern unsigned long long __attribute__(( aligned( 16 ) )) stack[];

// Busy-wait for lock to be zero
// Write the (non-zero) address of lock to lock to lock the stack
// Ensure the lock is mapped, first (not really interrupt-safe) FIXME
#define LOCK_DRIVER_STACK( fn )       \
    "\n\tadr  x9, stack_lock"   \
  "\n0:\tldxr x10, [x9]"        \
    "\n\tcbnz x10, 0b"          \
    "\n\tstxr w10, x9, [x9]"    \
    "\n\tcbnz x10, 0b"          \
    "\n\tadr  x10, stack" \
    "\n\tadd sp, x10, #8*"STACK_SIZE_STRING( STACK_SIZE ) \
    "\n\tbl "#fn \
    /* Unlock stack */ \
    "\n\tadr  x9, stack_lock" \
    "\n\tstr xzr, [x9]" \
    "\n\tsvc 0xfffd" \
    "\n"#fn"_throw_unnamed_exception:" \
    "\n\tadr  x9, stack_lock" \
    "\n\tstr xzr, [x9]" \
    "\n\tsvc 0xfffb" // FIXME

#define DRIVER_INITIALISER( entry ) \
asm ( ".section .init" \
    "\n.global _start" \
    "\n.type _start, %function" \
    "\n_start:" \
    "\n\tadr  x9, system" \
    "\n\tstr  x0, [x9]" \
    LOCK_DRIVER_STACK( entry ) \
    "\n.previous" )


asm ( ".section .text"
    "\nobject_to_return:"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xfff9"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\nobject_to_pass_to:"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xfff8"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\nduplicate_to_pass_to:"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xfff7"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\nduplicate_to_return:"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xfff6"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\n.previous" );

extern Object duplicate_to_return( Object original );
extern Object duplicate_to_pass_to( Object object, Object original );
extern Object object_to_return( void *handler, uint64_t value );
extern Object object_to_pass_to( Object user, void *handler, uint64_t value );


// This is just a temporary approach, to be replaced with cproto/crc32 stuff, TBD
enum SystemCall {
  is_a
  , DRIVER_SYSTEM_get_device_page
  , DRIVER_SYSTEM_map_at
  , DRIVER_SYSTEM_get_physical_memory_block
  , DRIVER_SYSTEM_register_service
  , DRIVER_SYSTEM_get_service
  , DRIVER_SYSTEM_get_core_timer_value
  , DRIVER_SYSTEM_get_core_interrupts_count
  , DRIVER_SYSTEM_get_ms_timer_ticks
  , DRIVER_SYSTEM_register_interrupt_handler
  , DRIVER_SYSTEM_remove_interrupt_handler
  , DRIVER_SYSTEM_create_thread
  
  // Implemented in kernel (at the moment)
  , DRIVER_SYSTEM_physical_address_of = 999
};

asm ( ".section .text"
    "\ninter_map_call_0p:"
    "\ninter_map_call_1p:"
    "\ninter_map_call_2p:"
    "\ninter_map_call_3p:"
    "\ninter_map_procedure_0p:"
    "\ninter_map_procedure_1p:"
    "\ninter_map_procedure_2p:"
    "\ninter_map_procedure_3p:"
    "\ninter_map_call_returning_object_0p:"
    "\ninter_map_call_returning_object_1p:"
    "\ninter_map_call_returning_object_2p:"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xfffe"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\n.previous" );

asm ( ".section .text"
    "\nyield:"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xfffc"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\n.previous" );

extern bool yield();
extern void inter_map_procedure_0p( Object target, uint64_t call );
extern void inter_map_procedure_1p( Object target, uint64_t call, uint64_t p1 );
extern void inter_map_procedure_2p( Object target, uint64_t call, uint64_t p1, uint64_t p2 );
extern void inter_map_procedure_3p( Object target, uint64_t call, uint64_t p1, uint64_t p2, uint64_t p3 );
extern Object inter_map_call_returning_object_0p( Object target, uint64_t call );
extern Object inter_map_call_returning_object_1p( Object target, uint64_t call, uint64_t p1 );
extern Object inter_map_call_returning_object_2p( Object target, uint64_t call, uint64_t p1, uint64_t p2 );
extern uint64_t inter_map_call_0p( Object target, uint64_t call );
extern uint64_t inter_map_call_1p( Object target, uint64_t call, uint64_t p1 );
extern uint64_t inter_map_call_2p( Object target, uint64_t call, uint64_t p1, uint64_t p2 );
extern uint64_t inter_map_call_3p( Object target, uint64_t call, uint64_t p1, uint64_t p2, uint64_t p3 );

// Always a single page of read-write, device memory; only for drivers' use
static inline Object get_device_page( uint64_t page_start )
{
  return inter_map_call_returning_object_1p( system, DRIVER_SYSTEM_get_device_page, page_start );
}

static inline Object get_physical_memory_block( uint64_t page_start, uint32_t length )
{
  return inter_map_call_returning_object_2p( system, DRIVER_SYSTEM_get_physical_memory_block, page_start, length );
}

static inline void map_physical_block_at( Object physical_block, uint64_t virtual_address )
{
  inter_map_call_2p( system, DRIVER_SYSTEM_map_at, (uint64_t) physical_block, virtual_address );
}

static inline uint64_t physical_address_of( void *virtual_address )
{
  return inter_map_call_1p( system, DRIVER_SYSTEM_physical_address_of, (uint64_t) virtual_address );
}

static inline uint32_t create_thread( void *code, uint64_t *stack_top, void *thread_local_storage )
{
  return inter_map_call_3p( system, DRIVER_SYSTEM_create_thread, (uint64_t) code, (uint64_t) stack_top, (uint64_t) thread_local_storage );
}

static inline void register_interrupt_handler( Object handler, int interrupt )
{
  inter_map_procedure_2p( system, DRIVER_SYSTEM_register_interrupt_handler, handler, interrupt );
}

static inline void remove_interrupt_handler( Object handler, int interrupt )
{
  inter_map_procedure_2p( system, DRIVER_SYSTEM_remove_interrupt_handler, handler, interrupt );
}

static inline void register_service( uint32_t name, Object service )
{
  inter_map_procedure_2p( system, DRIVER_SYSTEM_register_service, name, service );
}

static inline Object get_service( uint32_t name )
{
  return inter_map_call_1p( system, DRIVER_SYSTEM_get_service, name );
}

static inline uint64_t get_core_timer_value()
{
  return inter_map_call_0p( system, DRIVER_SYSTEM_get_core_timer_value );
}

static inline uint64_t get_core_interrupts_count()
{
  return inter_map_call_0p( system, DRIVER_SYSTEM_get_core_interrupts_count );
}

static inline uint64_t get_core_ms_timer_ticks()
{
  return inter_map_call_0p( system, DRIVER_SYSTEM_get_ms_timer_ticks );
}


// Returns number of interrupts detected before the call returned.
// 0 Means the thread was paused (this is expected behaviour)
// 1 Means the interrupt occurred before wait_until_woken was called.
// >1 Means multiple interrupts occurred
// TODO Add timeout parameter (return -ve on timeout)
static inline uint64_t wait_until_woken()
{
  uint64_t result;
  asm volatile ( "mov x0, #0\n\tsvc 0xfff5" : [res] "=r" (result) : : );
  return result;
}

static inline void wake_thread( uint32_t thread )
{
  asm volatile ( "svc 0xfff5" : : "r" (thread) );
}
