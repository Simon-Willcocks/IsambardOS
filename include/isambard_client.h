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

#define ISAMBARD_STACK( name, size_in_dwords ) uint64_t __attribute__(( aligned( 16 ) )) name[size_in_dwords]

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

#define RESTORE_SP_ON_ENTRY_TO_HANDLER \
        "\n\t1:" \
        "\n\tmov sp, x29" \
        "\n\tldr x29, [sp]" \
        "\n\tcbnz x29, 1b"

#if 0
#define FPR_LAYOUT                      \
        REG_PAIR ( d8,  d9, 112);       \
        REG_PAIR (d10, d11, 128);       \
        REG_PAIR (d12, d13, 144);       \
        REG_PAIR (d14, d15, 160);
#endif

#define THREADPOOL( label, size, count ) \
    struct label##_threadpool { uint64_t frames[count][size]; } __attribute__(( aligned(16) )) label##_threadpool; \
    struct label##_threadpool *label##_threadpool_free = 0; \
    uint32_t label##_threadpool_waiting_thread = 0; \
    void label##_initialise() { for (int i = 1; i < count; i++) { label##_threadpool.frames[i][size-1] = (uint64_t) label##_threadpool.frames[i]; }; label##_threadpool_free = &label##_threadpool+1; }

// free is a uint64_t *, and points to the top dword of the first free stack (i.e. on an 8-byte boundary)
// lock is a uint64_t, initialised to zero, it will be claimed for the *taking* of a stack from the pool, only
// waiting_thread is a uint32_t containing the thread code of a thread waiting for a stack
// Description:
//   Thread claims lock, to be allowed to view/modify free
//   Before checking free, stores the thread ID in waiting_thread
//     Threads relinquishing their stack will use ldxr/stxr to update free
//     The only reason the lock holder can fail to write to free is if another stack has become free in the meantime
//   Read the content of free. If zero, wait_until_woken (preserving x0, x1, clobbers x16, x17), retry
//   Read the pointer to the next stack, try to store it in free, if fails, retry (a stack has been relinquished)
//   Release the lock, make the call.
#define ISAMBARD_PROVIDER_THREADPOOL( type, return_functions, label ) \
        asm ( "\t.section .text" \
        "\n"#type"__veneer:" \
        "\n\tadr x17, "#label"_threadpool_lock" \
        CLAIM_LOCK \
        "\n\tadr x16, "#label"_threadpool_waiting_thread" \
        "\n\tstr w18, [x16]" \
        "\n\tadr x16, "#label"_threadpool_free" \
        \
        "\n2:" \
        "\n\tdsb sy" \
        "\n\tldxr x30, [x16]" \
        "\n\tadd sp, x30, #8" \
        "\n\tcbnz x30, 1f" \
        \
        "\n\tclrex" \
        "\n\tmov x29, x0" \
        "\n\tmov x30, x1" \
        "\n\rmov x0, #0" \
        "\n\rmov x1, #0" \
        "\n\tsvc " ENSTRING( ISAMBARD_GATE ) \
        "\n\tmov x0, x29" \
        "\n\tmov x1, x30" \
        "\n\tadr x16, "#label"_threadpool_free" \
        "\n\tadr x17, "#label"_threadpool_lock" \
        "\n\tb 2b" \
        \
        "\n1:" \
        "\n\tldr x29, [x30]" \
        "\n\tstxr w30, x29, [x16]" \
        "\n\tcbnz w30, 2b" \
        "\n\tadr x16, "#label"_threadpool_waiting_thread" \
        "\n\tstr wzr, [x16]" \
        RELEASE_LOCK \
        "\n\tmov x29, #0" \
        "\n\tbl "#type"__call_handler" \
        "\n\tldr w0, badly_written_driver_exception" \
        "\n"#type"__exception:" \
        RESTORE_SP_ON_ENTRY_TO_HANDLER \
        RESTORE_CALLEE_SAVED_REGISTERS \
"\nmov x27, x0" \
        "\n\tsvc #"ENSTRING( ISAMBARD_EXCEPTION ) \
        return_functions \
        "\n"#type"__return:" \
        RESTORE_SP_ON_ENTRY_TO_HANDLER \
        RESTORE_CALLEE_SAVED_REGISTERS \
        "\n\tsvc #"ENSTRING( ISAMBARD_RETURN ) \
        "\n\t.previous" );

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
"\nmov x27, x0" \
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
"\nmov x27, x0" \
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

#define ISAMBARD_PROVIDER_PER_OBJECT_LOCK_AND_STACK( type, return_functions, log2_total_size ) \
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
static inline name name##__from_integer_register( integer_register r ) { name result = { .r = r }; return result; } \
static inline name name##__duplicate_to_return( name o ) { name result; result.r = duplicate_to_return( (integer_register) o.r ); return result; } \
static inline name name##__duplicate_to_pass_to( integer_register target, name o ) { name result; result.r = duplicate_to_pass_to( target, (integer_register) o.r ); return result; }

ISAMBARD_INTERFACE( NUMBER )
// Special, for NUMBER only:
static inline NUMBER NUMBER__from_pointer( void *p ) { NUMBER result = { .r = (integer_register) p }; return result; }

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

// For Virtual Machine implementations
typedef uint64_t (*vm)( uint64_t pc, uint64_t syndrome, uint64_t fault_address, uint64_t intermediate_physical_address );
extern uint64_t switch_to_partner( vm handler, uint64_t pc );
extern uint64_t get_partner_register( int code );
extern void set_partner_register( int code, uint64_t value );

typedef enum { CNTKCTL_EL1, CSSELR_EL1, MAIR_EL1, SCTLR_EL1, TCR_EL1, TTBR0_EL1, TTBR1_EL1, VBAR_EL1, DACR32_EL2, CONTEXTIDR_EL1, ACTLR_EL1, AIDR_EL1, LAST_SYSTEM_REGISTER } vm_system_register;
extern void set_vm_system_register( vm_system_register reg, uint32_t v );
