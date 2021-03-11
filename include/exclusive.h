
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
/*
static inline void __attribute__(( always_inline )) dsb()
{
  asm volatile ( "dsb sy" );
}
*/

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

static void claim_lock( uint64_t volatile *var )
{
	// FIXME: will try to block even if it's the owner of the lock
  asm ( "\n\tmov x17, %[lock]"
        "\n\t2:"
        "\n\tldxr x16, [x17]"
        "\n\tcbz x16, 0f"
        "\n\tsvc 0xfffa"
        "\n\tb 1f"
        "\n0:"
        "\n\tstxr w16, x18, [x17]"
        "\n\tcbnz x16, 2b"
        "\n1:" : : [lock] "r" (var) );
}

static void release_lock( uint64_t volatile *var )
{
	// FIXME: throw exception if not the owner of the lock
  asm ( "\n\tmov x17, %[lock]"
        "\n\t2:"
        "\n\tldxr x16, [x17]"
        "\n\tcmp x16, x18"
        "\n\tbeq 0f"
        "\n\tsvc 0xfffb"
        "\n\tb 1f"
        "\n0:"
        "\n\tstxr w16, xzr, [x17]"
        "\n\tcbnz x16, 2b"
        "\n1:" : : [lock] "r" (var) );
}

