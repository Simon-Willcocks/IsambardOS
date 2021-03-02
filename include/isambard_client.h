
extern Object duplicate_to_return( Object original );
extern Object duplicate_to_pass_to( Object object, Object original );
extern Object object_to_return( void *handler, uint64_t value );
extern Object object_to_pass_to( Object user, void *handler, uint64_t value );

#define ISAMBARD_PROVIDER( type, switches ) integer_register type##_call_handler( type o, integer_register call, integer_register p1, integer_register p2, integer_register p3 ) { p1=p1; p2=p2; p3=p3; o = o; switches; return unknown_call( call ); }

// The following macros are used to provide the veneers (assembly code, to optionally claim a lock,
// establish a stack, and call the call handler for the type) for the various types of provider

// FIXME: Deal with exceptions, releasing the locks, etc.
#define ISAMBARD_PROVIDER_UNLOCKED_PER_OBJECT_STACK( type ) asm ( "\t.section .text\n"#type"_veneer: mov sp, x0\n\tbl "#type"_call_handler\n\tsvc 0xfffd\n\t.previous" );
#define ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( type, lock, stack, stack_size ) asm ( "\t.section .text\n"#type"_veneer: adr x17, "#stack"\n\tadd sp, x17, #"#stack_size"\n\tbl "#type"_call_handler\n\tsvc 0xfffd\n\t.previous" );

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
extern void Isambard_10( integer_register o, uint32_t call, integer_register p0 ); 
extern integer_register Isambard_11( integer_register o, uint32_t call, integer_register p0 ); 
extern void Isambard_20( integer_register o, uint32_t call, integer_register p0, integer_register p1 ); 
extern integer_register Isambard_21( integer_register o, uint32_t call, integer_register p0, integer_register p1 ); 
extern void Isambard_30( integer_register o, uint32_t call, integer_register p0, integer_register p1, integer_register p2 ); 
extern integer_register Isambard_31( integer_register o, uint32_t call, integer_register p0, integer_register p1, integer_register p2 );

// FIXME: this assumes a single source file for each driver
asm (
"Isambard_00:\n"
"Isambard_10:\n"
"Isambard_20:\n"
"Isambard_30:\n"
"Isambard_01:\n"
"Isambard_11:\n"
"Isambard_21:\n"
"Isambard_31:\n"
    "\n\tstp x29, x30, [sp, #-16]!"
    "\n\tsvc 0xfffe"
    "\n\tldp x29, x30, [sp], #16"
    "\n\tret"
    "\n.previous" );
