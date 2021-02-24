#ifndef assert
#define assert(x)
#define delete_assert
#endif

#define blocks( min, max, action, maxaction )\
{\
  assert( 0 == (p & ((1ull<<min)-1)) ); \
  while ((end-p) >= (1ull<<min)) {\
    if (max != 0 && (end - p) >= (1ull<<max) && 0 == (p & ((1ull<<max)-1))) {\
      maxaction;\
      assert( end >= p );\
    }\
    else {\
      unsigned long long b = p;\
      action;\
      assert( p - b == (1ull<<min) );\
    }\
    assert( 0 == (p & ((1ull<<min)-1)) );\
  }\
}

#ifdef delete_assert
#undef assert
#undef delete_assert
#endif

// The macro assumes integer variables p and end, p must be on a (1<<min) boundary on entry, end must be on a
// boundary of the outermost block. action will NOT be called at end.
// action will be executed for each block at this level, and must increment p by the size of the block (1<<min)
// maxaction will be executed when p reaches a (1<<max) boundary, and must increment p by a multiple of (1<<max)

// Examples of use are memset and initialising free memory area
