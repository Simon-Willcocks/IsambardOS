
extern Object duplicate_to_return( Object original );
extern Object duplicate_to_pass_to( Object object, Object original );
extern Object object_to_return( void *handler, uint64_t value );
extern Object object_to_pass_to( Object user, void *handler, uint64_t value );

#define ISAMBARD_PROVIDER( type, switches ) integer_register type##_call_handler( type o, integer_register call, integer_register p1, integer_register p2, integer_register p3, integer_register p4 ) { p1=p1; p2=p2; p3=p3; p4=p4; o = o; switches; return unknown_call( call ); }

// The following macros are used to provide the veneers (assembly code, to optionally claim a lock,
// establish a stack, and call the call handler for the type) for the various types of provider
// Stacks SHALL BE 16-byte aligned. The variables have to be non-static, for the inline assembly to see them at link time.

// Stacks: per object, from pool, shared (one thread at a time) / Recursion supported?

// FIXME: Deal with exceptions, releasing the locks, etc.
// TODO: Release and return svc (x17 -> lock)?
// Maybe return at top level is done by making a call? That would allow for returning multiple values (or exceptions).

#define ISAMBARD_PROVIDER_UNLOCKED_PER_OBJECT_STACK( type ) asm ( "\t.section .text\n"#type"_veneer: mov sp, x0\n\tbl "#type"_call_handler\n\tsvc 0xfffd\n\t.previous" );

#define ENSTRING( n ) #n
#define ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( type, lock, stack, stack_size ) \
        asm ( "\t.section .text" \
        "\n"#type"_veneer: adr x17, "#stack \
        "\n\tadd sp, x17, #" ENSTRING( stack_size ) \
        "\n\tadr x17, "#lock \
        "\n\t2:" \
        "\n\tldxr x16, [x17]" \
        "\n\tcbz x16, 0f" \
        "\n\tsvc 0xfffa" \
        "\n\tb 1f" \
        "\n0:" \
        "\n\tstxr w16, x18, [x17]" \
        "\n\tcbnz x16, 2b" \
        "\n1:" \
        "\n\tbl "#type"_call_handler" \
        "\n\tadr x17, "#lock \
        "\n\tsvc 0xfffb" \
        "\n\tsvc 0xfffd" \
        "\n\t.previous" );

#define ISAMBARD_INTERFACE( name ) \
typedef struct { integer_register r; } name; \
static inline name name##_from_integer_register( integer_register r ) { name result = { .r = r }; return result; } \
static inline name name##_duplicate_to_return( name o ) { name result; result.r = duplicate_to_return( (integer_register) o.r ); return result; } \
static inline name name##_duplicate_to_pass_to( integer_register target, name o ) { name result; result.r = duplicate_to_pass_to( target, (integer_register) o.r ); return result; }

ISAMBARD_INTERFACE( NUMBER )
// Special, for NUMBER only:
static inline NUMBER NUMBER_from_pointer( void *p ) { NUMBER result = { .r = (integer_register) p }; return result; }

extern integer_register unknown_call( integer_register call );

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

