
static inline uint64_t load_exclusive( uint64_t volatile *mem )
{
  uint64_t result;
  asm volatile ( "\n\tldxr %[result], [%[memory]]"
      : [result] "=&r" (result) : [memory] "r" (mem) );
  return result;
}

static inline bool store_exclusive( uint64_t volatile *mem, uint64_t value )
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

static inline void __attribute__(( always_inline )) dsb()
{
  asm volatile ( "dsb sy" );
}

static void clearbit( uint64_t volatile *var, int bit )
{
  for (;;) {
    uint64_t v = load_exclusive( var );
    if (store_exclusive( var, v & ~(1ull << bit) ))
      return;
  };
}

static void setbit( uint64_t volatile *var, int bit )
{
  for (;;) {
    uint64_t v = load_exclusive( var );
    if (store_exclusive( var, v | (1ull << bit) ))
      return;
  };
}

