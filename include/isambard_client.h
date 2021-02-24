typedef struct { integer_register r; } NUMBER;

static inline NUMBER NUMBER_from_REGISTER( REGISTER r ) { NUMBER result = { .r = r }; return result; }

extern void Isambard_00( integer_register o, uint32_t call ); 
extern NUMBER Isambard_01( integer_register o, uint32_t call ); 
extern void Isambard_10( integer_register o, uint32_t call, integer_register p0 ); 
extern NUMBER Isambard_11( integer_register o, uint32_t call, integer_register p0 ); 
extern void Isambard_20( integer_register o, uint32_t call, integer_register p0, integer_register p1 ); 
extern NUMBER Isambard_21( integer_register o, uint32_t call, integer_register p0, integer_register p1 ); 
extern void Isambard_30( integer_register o, uint32_t call, integer_register p0, integer_register p1, integer_register p2 ); 
extern NUMBER Isambard_31( integer_register o, uint32_t call, integer_register p0, integer_register p1, integer_register p2 );

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
"svc 0xfffe" );

