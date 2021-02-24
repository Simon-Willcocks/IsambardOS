
// An Aarch64 vector table consists of 16, 128-byte (32 instruction) blocks of code, starting on a 2k boundary.
// The first eight entries are for exceptions coming from the current exception level, the
// second eight are for exceptions coming from a lower exception level.
// The first four entries use EL0's SP (clarify TODO)
// The second four use the current EL's SP
// The third four are exceptions from aarch64
// The final four are exceptions from aarch32
// In order, the exceptions are:
//    Synchronous (undefined instruction, software interrupt, etc.)
//    IRQ
//    FIQ
//    SError (possibly non-precise, clarify TODO)
//
// This file requires the following macros to be defined, 17 in all, the first being the name you want to use
// for the vector table, to pass into the appropriate VBAR, e.g.
//   asm volatile ( "\tmsr VBAR_EL3, %[table]\n" : : [table] "r" (VBAR_EL3) );
// The rest are stings containing aarch64 code to handle the appropriate exception.
// This file is completely independent of the code being inserted, it just ensures that the result is a somewhat
// valid table (16 entries, less than 2k in total, and properly on a 2k boundary).

#ifndef MACRO_AS_STRING
#define MACRO_AS_STRING2( number ) #number
#define MACRO_AS_STRING( number ) MACRO_AS_STRING2( number )
#endif

#ifndef AARCH64_VECTOR_TABLE_NAME
#error "Please define AARCH64_VECTOR_TABLE_NAME - the name you want for the table"
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_SYNC_CODE
#error "Please define AARCH64_VECTOR_TABLE_SP0_SYNC_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_IRQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_SP0_IRQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_FIQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_SP0_FIQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_SP0_SERROR_CODE
#error "Please define AARCH64_VECTOR_TABLE_SP0_SERROR_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_SYNC_CODE
#error "Please define AARCH64_VECTOR_TABLE_SPX_SYNC_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_IRQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_SPX_IRQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_FIQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_SPX_FIQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_SPX_SERROR_CODE
#error "Please define AARCH64_VECTOR_TABLE_SPX_SERROR_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_IRQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH64_IRQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_FIQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH64_FIQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH64_SERROR_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SERROR_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_SYNC_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SYNC_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_IRQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH32_IRQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_FIQ_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH32_FIQ_CODE"
#endif

#ifndef AARCH64_VECTOR_TABLE_LOWER_AARCH32_SERROR_CODE
#error "Please define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SERROR_CODE"
#endif

// Quick note about compile-time checks; the offset between two labels is non-constant if there is an .align
// between the two: "Error: non-constant expression in ".if" statement"

#define AARCH64_VECTOR_TABLE_EXPAND_CODE( event ) \
  asm volatile ( "\t.balign 0x80\n\t"MACRO_AS_STRING( AARCH64_VECTOR_TABLE_NAME ) "_" #event ":" ); \
  AARCH64_VECTOR_TABLE_##event##_CODE \
  asm volatile ( ".ifeq . - "MACRO_AS_STRING( AARCH64_VECTOR_TABLE_NAME ) "_" #event \
               "\n\t.error AARCH64_VECTOR_TABLE_" #event "_CODE too short (0 instructions)" \
	       "\n.endif" ); \
  asm volatile ( ".ifgt . - "MACRO_AS_STRING( AARCH64_VECTOR_TABLE_NAME ) "_" #event " - 0x80" \
               "\n\t.error AARCH64_VECTOR_TABLE_" #event "_CODE too long (over 32 instructions)" \
	       "\n.endif" )

void __attribute__(( noreturn, aligned( 0x800 ) )) AARCH64_VECTOR_TABLE_NAME()
{
  asm volatile ( ".ifne . - " MACRO_AS_STRING( AARCH64_VECTOR_TABLE_NAME )
               "\n\t.error \"Table not starting on 2k boundary, check compiler options!\""
	       "\n.endif" );

  AARCH64_VECTOR_TABLE_EXPAND_CODE( SP0_SYNC );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( SP0_IRQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( SP0_FIQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( SP0_SERROR );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( SPX_SYNC );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( SPX_IRQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( SPX_FIQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( SPX_SERROR );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH64_SYNC );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH64_IRQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH64_FIQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH64_SERROR );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH32_SYNC );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH32_IRQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH32_FIQ );
  AARCH64_VECTOR_TABLE_EXPAND_CODE( LOWER_AARCH32_SERROR );

/* These checks do not work, "Error: non-constant expression in ".if" statement" because the assembler doesn't know
 * the first .align instruction has no effect. Maybe, one day, it will manage.
 *   asm volatile ( ".ifgt . - " MACRO_AS_STRING( AARCH64_VECTOR_TABLE_NAME )" - 0x800"
 *                "\n\t.error \"Table too big, at least one item of code is probably more than 32 instructions!\""
 * 	       "\n.endif" );
 *   // A second ".balign 0x80" directly after a previous one will not move the address, at least one instruction is
 *   // needed.
 *   asm volatile ( ".iflt . - " MACRO_AS_STRING( AARCH64_VECTOR_TABLE_NAME ) " - 0x784"
 *                "\n\t.error \"Table too small, each entry must include at least one instruction!\""
 * 	       "\n.endif" );
 */

#ifdef AARCH64_VECTOR_TABLE_SUPPORT_CODE
  asm volatile ( AARCH64_VECTOR_TABLE_SUPPORT_CODE );
#endif
  __builtin_unreachable();
}

#define JOIN( p, e ) p##e
#define VECTOR_CHOICE_FUNCTION( name )  static inline void JOIN( Choose_, name )() \
{ asm volatile ( "\tmsr VBAR_EL" MACRO_AS_STRING( HANDLER_EL ) ", %[table]\n" : : [table] "r" (name) ); }

VECTOR_CHOICE_FUNCTION( AARCH64_VECTOR_TABLE_NAME )
