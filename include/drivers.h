#include "types.h"

#ifdef __aarch64__
register uint32_t this_thread asm("x18");
#endif

void entry();

typedef integer_register Object;

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

#include "isambard_client.h"
ISAMBARD_INTERFACE( DRIVER_SYSTEM )
ISAMBARD_INTERFACE( SYSTEM )
ISAMBARD_INTERFACE( SERVICE )
ISAMBARD_INTERFACE( PHYSICAL_MEMORY_BLOCK )
ISAMBARD_INTERFACE( INTERRUPT_HANDLER )
#include "interfaces/client/SYSTEM.h"
#include "interfaces/client/DRIVER_SYSTEM.h"

extern integer_register stack_lock;
extern SYSTEM system; // Initialised by _start code
extern integer_register __attribute__(( aligned( 16 ) )) stack[]; // Sized in one driver C file

static inline DRIVER_SYSTEM driver_system() { return DRIVER_SYSTEM_from_integer_register( system.r ); }

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

static inline integer_register create_thread( void *code, uint64_t *stack_top )
{
  return SYSTEM__create_thread( system, NUMBER_from_integer_register( (integer_register) code ), NUMBER_from_integer_register( (integer_register) stack_top ) ).r;
}


// crc32 code taken from https://create.stephan-brumme.com/crc32/
// Not for long strings or binary data.

static inline uint32_t crc32(const void* data, uint32_t previousCrc32)
{
  const uint32_t Polynomial = 0xEDB88320;

  uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
  unsigned char* current = (unsigned char*) data;
  while ('\0' != *current) {
    crc ^= *current++;
    for (unsigned int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & Polynomial);
    }
  }
  return ~crc; // same as crc ^ 0xFFFFFFFF
}

static inline NUMBER name_code( const char *name )
{
  return NUMBER_from_integer_register( crc32( name, 0 ) );
}

static inline SERVICE get_service( const char *name )
{
  return SYSTEM__get_service( system, name_code( name ) );
}

static inline void register_service( const char *name, SERVICE service )
{
  SYSTEM__register_service( system, name_code( name ), service );
}


// Returns number of wake_thread detected before the call returned.
// 0 Means the thread was paused (this is expected behaviour)
// 1 Means the wake_thread occurred before wait_until_woken was called.
// >1 Means multiple wake_threads occurred
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
