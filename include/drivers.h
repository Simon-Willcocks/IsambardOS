#include "types.h"

#ifdef __aarch64__
register uint32_t this_thread asm("x18");
#endif

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

#include "isambard_client.h"

static inline NUMBER __attribute__ ((pure)) name_code( const char *name )
{
  // crc32 code taken from https://create.stephan-brumme.com/crc32/
  // Not for long strings or binary data!

  const uint32_t Polynomial = 0xEDB88320;

  uint32_t crc = 0xFFFFFFFF;
  const unsigned char* current = (const unsigned char*) name;
  while ('\0' != *current) {
    crc ^= *current++;
    for (unsigned int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & Polynomial);
    }
  }
  return NUMBER_from_integer_register( ~crc );
}

static inline void sleep_ms( integer_register timeout );
ISAMBARD_INTERFACE( SYSTEM )
extern SYSTEM system; // Initialised by _start code


ISAMBARD_INTERFACE( DRIVER_SYSTEM )
ISAMBARD_INTERFACE( SERVICE )
ISAMBARD_INTERFACE( PHYSICAL_MEMORY_BLOCK )
ISAMBARD_INTERFACE( INTERRUPT_HANDLER )
#include "interfaces/client/SYSTEM.h"
#include "interfaces/client/DRIVER_SYSTEM.h"

extern integer_register stack_lock;
extern integer_register __attribute__(( aligned( 16 ) )) stack[]; // Sized in libdriver.c

static inline DRIVER_SYSTEM driver_system() { return DRIVER_SYSTEM_from_integer_register( system.r ); }

extern bool yield();

static inline integer_register create_thread( void *code, uint64_t *stack_top )
{
  return SYSTEM__create_thread( system, NUMBER_from_integer_register( (integer_register) code ), NUMBER_from_integer_register( (integer_register) stack_top ) ).r;
}

/* Accesses to the same peripheral will always arrive and return in-order. It is only when
 * switching from one peripheral to another that data can arrive out-of-order. The simplest way
 * to make sure that data is processed in-order is to place a memory barrier instruction at critical
 * positions in the code. You should place:
 * * A memory write barrier before the first write to a peripheral.
 * * A memory read barrier after the last read of a peripheral.
 * BCM2835 ARM Peripherals p. 7
*/
static inline void memory_write_barrier()
{
  asm ( "dsb sy" ); // Excessive
}

static inline void memory_read_barrier()
{
  asm ( "dsb sy" ); // Excessive
}

// Returns number of wake_thread detected before the call returned.
// 0 Means the thread was paused (this is expected behaviour)
// 1 Means the wake_thread occurred before wait_until_woken was called.
// >1 Means multiple wake_threads occurred
// TODO Add timeout parameter (return -ve on timeout)
extern integer_register gate_function( uint32_t thread, integer_register timeout );

static inline void wake_thread( uint32_t thread )
{
  gate_function( thread, 0 );
}

static inline integer_register wait_until_woken()
{
  return gate_function( 0, 0 );
}

static inline integer_register sleep_unless_woken( integer_register timeout )
{
  return gate_function( 0, timeout );
}

static inline void sleep_ms( integer_register timeout )
{
  if (timeout <= 0) yield(); else gate_function( 0, timeout );
}
