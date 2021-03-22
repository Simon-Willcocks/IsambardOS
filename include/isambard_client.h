#include "isambard_syscalls.h"

extern Object duplicate_to_return( Object original );
extern Object duplicate_to_pass_to( Object object, Object original );
extern Object interface_to_return( void *handler, void * value );
extern Object interface_to_pass_to( Object user, void *handler, void * value );

#define ISAMBARD_PROVIDER( type, switches ) void __attribute__ ((noreturn)) type##__call_handler( type o, integer_register call, integer_register p1, integer_register p2, integer_register p3, integer_register p4 ) { p1=p1; p2=p2; p3=p3; p4=p4; o = o; switches; type##__exception( 0xbadc0de1 ); }

// The following macros are used to provide the veneers (assembly code, to optionally claim a lock,
// establish a stack, and call the call handler for the type) for the various types of provider
// Stacks SHALL BE 16-byte aligned. The variables have to be non-static, for the inline assembly to see them at link time.

// Stacks: per object, from pool, shared (one thread at a time) / Recursion supported?

// FIXME: Deal with exceptions, releasing the locks, etc.
// TODO: Release and return svc (x17 -> lock)?
// Maybe return at top level is done by making a call? That would allow for returning multiple values (or exceptions).

#define ENSTRING2( n ) #n
#define ENSTRING( n ) ENSTRING2( n )

#define CLAIM_LOCK \
        "\n\t2:" \
        "\n\tldxr x16, [x17]" \
        "\n\tcbz x16, 0f" \
        "\n\tsvc #"ENSTRING( ISAMBARD_LOCK_WAIT ) \
        "\n\tb 1f" \
        "\n0:" \
        "\n\tstxr w16, x18, [x17]" \
        "\n\tcbnz x16, 2b" \
        "\n1:"

// TODO release and return syscall?
#define RELEASE_LOCK \
        "\n\tldxr x16, [x17]" \
        "\n\tcmp x16, x18" \
        "\n\tbne 1f" \
        "\n\tstxr w16, xzr, [x17]" \
        "\n\tcbz w16, 2f" \
        "\n\t1:" \
        "\n\tsvc #"ENSTRING( ISAMBARD_LOCK_RELEASE ) \
        "\n\t2:"

#define STORED_REGISTER_SPACE (16 * 6)

// The registers that have to be restored on function return
#define STACK_CALLEE_SAVED_REGISTERS \
"\n\tstp x19, x20, [sp, #-16*6]!" \
"\n\tstp x29, x30, [sp, #5 * 16]" \
"\n\tstp x27, x28, [sp, #4 * 16]" \
"\n\tstp x25, x26, [sp, #3 * 16]" \
"\n\tstp x23, x24, [sp, #2 * 16]" \
"\n\tstp x21, x22, [sp, #1 * 16]"

// Restore them before releasing the lock!
#define RESTORE_CALLEE_SAVED_REGISTERS \
"\n\tldp x21, x22, [sp, #1 * 16]" \
"\n\tldp x23, x24, [sp, #2 * 16]" \
"\n\tldp x25, x26, [sp, #3 * 16]" \
"\n\tldp x27, x28, [sp, #4 * 16]" \
"\n\tldp x29, x30, [sp, #5 * 16]" \
"\n\tldp x19, x20, [sp], #16*6"

#if 0
#define FPR_LAYOUT                      \
        REG_PAIR ( d8,  d9, 112);       \
        REG_PAIR (d10, d11, 128);       \
        REG_PAIR (d12, d13, 144);       \
        REG_PAIR (d14, d15, 160);
#endif

#if 0
//This doesn't work : how do you set the stack pointer, for the return function?
#define ISAMBARD_PROVIDER_UNLOCKED_PER_OBJECT_STACK( type ) \
        asm ( "\t.section .text" \
              "\n"#type"_veneer: mov sp, x0" \
              STACK_CALLEE_SAVED_REGISTERS \
              "\n\tbl "#type"__call_handler" \
              "\n\tldr w0, badly_written_driver_exception" \
              "\n\tsvc #"ENSTRING( ISAMBARD_EXCEPTION ) \
              "\n"#type"__return:" \
              RESTORE_CALLEE_SAVED_REGISTERS \
              "\n\tsvc #"ENSTRING( ISAMBARD_RETURN ) \
              "\n\t.previous" );
#endif

#define ISAMBARD_PROVIDER_NO_LOCK_AND_SINGLE_STACK( type, return_functions, stack, stack_size ) \
        asm ( "\t.section .text" \
        "\n"#type"__veneer: adr x17, "#stack \
        "\n\tadd sp, x17, #" #stack_size \
        STACK_CALLEE_SAVED_REGISTERS \
        "\n\tbl "#type"__call_handler" \
        "\n\tldr w0, badly_written_driver_exception" \
        "\n"#type"__exception:" \
        "\n\tadr x17, "#stack \
        "\n\tadd sp, x17, #" #stack_size "-" ENSTRING( STORED_REGISTER_SPACE ) \
        RESTORE_CALLEE_SAVED_REGISTERS \
        "\n\tsvc #"ENSTRING( ISAMBARD_EXCEPTION ) \
        return_functions \
        "\n"#type"__return:" \
        "\n\tadr x17, "#stack \
        "\n\tadd sp, x17, #" #stack_size "-" ENSTRING( STORED_REGISTER_SPACE ) \
        RESTORE_CALLEE_SAVED_REGISTERS \
        "\n\tsvc #"ENSTRING( ISAMBARD_RETURN ) \
        "\n\t.previous" );

#define ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( type, return_functions, lock, stack, stack_size ) \
        asm ( "\t.section .text" \
        "\n"#type"__veneer: adr x17, "#stack \
        "\n\tadd sp, x17, #" #stack_size \
        "\n\tadr x17, "#lock \
        CLAIM_LOCK \
        STACK_CALLEE_SAVED_REGISTERS \
        "\n\tbl "#type"__call_handler" \
        "\n\tadr x17, "#lock \
        "\n\tsvc #"ENSTRING( ISAMBARD_LOCK_RELEASE ) \
        "\n\tldr w0, badly_written_driver_exception" \
        "\n"#type"__exception:" \
        "\n\tadr x17, "#stack \
        "\n\tadd sp, x17, #" #stack_size "-" ENSTRING( STORED_REGISTER_SPACE ) \
        RESTORE_CALLEE_SAVED_REGISTERS \
        "\n\tsvc #"ENSTRING( ISAMBARD_EXCEPTION ) \
        return_functions \
        "\n"#type"__return:" \
        "\n\tadr x17, "#stack \
        "\n\tadd sp, x17, #" #stack_size "-" ENSTRING( STORED_REGISTER_SPACE ) \
        RESTORE_CALLEE_SAVED_REGISTERS \
        "\n\tadr x17, "#lock \
        RELEASE_LOCK \
        "\n\tsvc #"ENSTRING( ISAMBARD_RETURN ) \
        "\n\t.previous" );

// Provides a type, a veneer, type-specific return and exception routines, macros for declaring
// object values.
// PER_OBJECT_LOCK_AND_STACK
//   __storage_unit : struct { uint64_t stack[...]; type object; [padding] uint64_t lock; uint64_t stacktop; }
//   __veneer: takes object pointer to initialise SP, claims lock, calls _handler
//   __return: takes unknown parameters, releases lock, and requests an inter-map return
//   __exception: takes unknown exception code in x0, releases lock, throws an inter-map exception
//
// It is the responsibility of the code to release all other locks claimed during the call
// before calling the return or exception routines. (TODO: a variation, where [padding] includes
// a call-local head of a list of claimed lock variables.
//   struct safelock { uint64_t lock; struct safelock *next; })
// The stack has to be aligned to log2_total_size, the bottom word = offset of object.
// One instruction overhead to store top of stack at top of object storage (for restoration
// of callee-saved registers).
// Lock should be 16 bytes before the end of the structure

struct ippolas_veneer_data {
  uint64_t lock;
  void *obj;
};

#define ISAMBARD_PROVIDER_PER_OBJECT_LOCK_AND_STACK( type, log2_total_size ) \
        struct type##_storage __attribute__(( aligned( 1 << log2_total_size ) )) { \
          uint64_t stack[((1 << log2_total_size) - sizeof( type ) - sizeof( ippolas_veneer_data ))/16]; \
          type object; \
          ippolas_veneer_data veneer_data; \
        }; \
        asm ( "\t.section .text" \
        "\n"#type"__veneer:" \
        "\n\tmov sp, x0" \
        "\n\torr x17, sp, #(1 << "#log2_total_size") - 15" \
        "\n\tstr x0, [x17, #8]" \
        CLAIM_LOCK \
        "\n\tbl "#type"__call_handler" \
        "\n\tldr w0, badly_written_driver_exception" \
        \
        "\n"#type"__exception:" \
        "\n\torr x17, sp, #(1 << "#log2_total_size") - 15" \
        "\n\tldr x16, [x17, #8]" \
        "\n\tsub sp, x16, #"ENSTRING( STORED_REGISTER_SPACE ) \
        RESTORE_CALLEE_SAVED_REGISTERS \
        RELEASE_LOCK \
        "\n\tsvc #"ENSTRING( ISAMBARD_EXCEPTION ) \
        \
        return_functions \
        "\n"#type"__return:" \
        "\n\torr x17, sp, #(1 << "#log2_total_size") - 15" \
        "\n\tldr x16, [x17, #8]" \
        "\n\tsub sp, x16, #"ENSTRING( STORED_REGISTER_SPACE ) \
        RESTORE_CALLEE_SAVED_REGISTERS \
        RELEASE_LOCK \
        "\n\tsvc #"ENSTRING( ISAMBARD_RETURN ) \
        "\n\t.previous" );

#define ISAMBARD_INTERFACE( name ) \
typedef struct { integer_register r; } name; \
static inline name name##_from_integer_register( integer_register r ) { name result = { .r = r }; return result; } \
static inline name name##_duplicate_to_return( name o ) { name result; result.r = duplicate_to_return( (integer_register) o.r ); return result; } \
static inline name name##_duplicate_to_pass_to( integer_register target, name o ) { name result; result.r = duplicate_to_pass_to( target, (integer_register) o.r ); return result; }

ISAMBARD_INTERFACE( NUMBER )
// Special, for NUMBER only:
static inline NUMBER NUMBER_from_pointer( void *p ) { NUMBER result = { .r = (integer_register) p }; return result; }

extern void Isambard_00( integer_register o, uint32_t call ); 
extern integer_register Isambard_01( integer_register o, uint32_t call ); 
extern void Isambard_10( integer_register o, uint32_t call, integer_register p1 ); 
extern integer_register Isambard_11( integer_register o, uint32_t call, integer_register p1 ); 
extern void Isambard_20( integer_register o, uint32_t call, integer_register p1, integer_register p2 ); 
extern integer_register Isambard_21( integer_register o, uint32_t call, integer_register p1, integer_register p2 ); 
extern void Isambard_30( integer_register o, uint32_t call, integer_register p1, integer_register p2, integer_register p3 ); 
extern integer_register Isambard_31( integer_register o, uint32_t call, integer_register p1, integer_register p2, integer_register p3 );
extern void Isambard_40( integer_register o, uint32_t call, integer_register p1, integer_register p2, integer_register p3, integer_register p4 ); 
extern integer_register Isambard_41( integer_register o, uint32_t call, integer_register p1, integer_register p2, integer_register p3, integer_register p4 );

