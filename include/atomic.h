/* Copyright (c) 2020 Simon Willcocks */

#ifndef ISAMBARD_ATOMIC
#define ISAMBARD_ATOMIC

#ifndef BEING_TESTED
static inline uint32_t load_exclusive_word( uint32_t volatile *mem )
{
  uint32_t result;
  asm volatile ( "\n\tldxr %w[result], [%[memory]]"
      : [result] "=&r" (result) : [memory] "r" (mem) );
  return result;
}

static inline bool store_exclusive_word( uint32_t volatile *mem, uint32_t value )
{
  uint32_t blocked;
  asm volatile ( "\n\tstxr %w[blocked], %w[value], [%[memory]]"
      : [blocked] "=&r" (blocked)
      : [memory] "r" (mem), [value] "r" (value)
      : "memory" );
  return !blocked;
}

static inline uint64_t load_exclusive_dword( uint64_t volatile *mem )
{
  uint64_t result;
  asm volatile ( "\n\tldxr %[result], [%[memory]]"
      : [result] "=&r" (result) : [memory] "r" (mem) );
  return result;
}

static inline bool store_exclusive_dword( uint64_t volatile *mem, uint64_t value )
{
  uint32_t blocked;
  asm volatile ( "\n\tstxr %w[blocked], %[value], [%[memory]]"
      : [blocked] "=&r" (blocked)
      : [memory] "r" (mem), [value] "r" (value)
      : "memory" );
  return !blocked;
}

static inline void clear_exclusive()
{
  asm volatile ( "\n\tclrex" );
}

static inline void claim_lock( volatile uint64_t *lock )
{
  uint64_t v;
  uint64_t me;

  // The stack pointer is associated with this core.
  asm volatile ( "\n\tmov %[me], sp" : [me] "=r" (me) );

  for (;;) {
    v = load_exclusive_dword( lock );
    if (v == 0) {
      if (store_exclusive_dword( lock, me ))
        return;
    }
    else
      clear_exclusive();
    // asm volatile( "wfe" ); maybe?
  };
}

static inline void release_lock( volatile uint64_t *lock )
{
  *lock = 0;
  asm volatile ( "dsb sy\nsevl" );
}

static inline void __attribute__(( always_inline ))dsb()
{
  asm volatile ( "dsb sy" );
}
#endif
#endif
