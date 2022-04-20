#include "vm_state.h"

extern vm_state secure_registers; // A place to store the secure mode registers

void initialise_shared_isambard_kernel_tables( Core *core0, int cores );
void set_asid( Core *core, uint64_t asid );

static const uint64_t himem_offset = 0xfffffffffe000000;

#ifndef EL_PARAMETERS
#define EL_PARAMETERS Core *core, int number, uint64_t *present
#define EL_ARGUMENTS core, number, present
#endif

typedef void __attribute__(( noreturn )) (*isambard_init_code)( EL_PARAMETERS );
typedef void __attribute__(( noreturn )) (*isambard_code_caller)( EL_PARAMETERS, isambard_init_code code );

typedef enum { WAITING, NORMAL, SPECIAL } core_types;

// Routine provided by chosen el3_ behaviour source file, called once, during
// initialisation.
// The routine may set present[number] = NORMAL and return, or set it to
// SPECIAL, not return, and run independently of the kernel.
extern void roll_call( core_types volatile *present, unsigned number );

// Routine provided by chosen el3_ behaviour source file, called once, during
// initialisation, finally entering `code', at Secure EL1 with no MMU.
extern void __attribute__(( noreturn )) el3_synchronised_initialise( EL_PARAMETERS );

// Entry routine for IsambardOS at EL1
extern void __attribute__(( noreturn )) isambard_secure_el1( Core *phys_core, int number, uint64_t *present );

extern void __attribute__(( noreturn )) el3_run_at_secure_el1( EL_PARAMETERS, isambard_init_code code );

extern void __attribute__(( noreturn )) el3_run_with_mmu( EL_PARAMETERS, isambard_init_code code );
