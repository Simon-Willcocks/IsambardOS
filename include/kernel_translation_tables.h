
void initialise_shared_isambard_kernel_tables( Core *core0, int cores );
void set_asid( Core *core, uint64_t asid );

// Registers set at EL1, secure and non-secure
// Order is fixed for el3_virtual_machines.c assembler
typedef struct {
  uint64_t cntkctl_el1;
  uint64_t csselr_el1;

  uint64_t mair_el1;
  uint64_t sctlr_el1;

  uint64_t tcr_el1;
  uint64_t ttbr0_el1; // Core-specific, in secure mode

  uint64_t ttbr1_el1;
  uint64_t vbar_el1;

  // uint64_t spsr_el1; Thread-specific
  // uint64_t elr_el1; Thread-specific
  // TPIDR_EL1

  uint64_t vttbr_el2;
  uint64_t hcr_el2;
  uint64_t hstr_el2;
  uint64_t vmpidr_el2;
  uint64_t vpidr_el2;
  uint64_t vtcr_el2;
  uint64_t dacr32_el2;
  uint64_t contextidr_el1;

  // contextidr_el2, CPTR_EL2, DACR32_EL2, HACR_EL2, RMR_EL2, RMR_EL2, TPIDR_EL2; No use for these registers
  // ESR_EL2, FAR_EL2, HPFAR_EL2, IFSR32_EL2; passed to partner thread to inform of exceptions
  // sctlr_el2, tcr_el2, mair_el2, vbar_el2; Relates to Isambard VM implementation, doesn't change
} vm_state;

extern vm_state vm[2]; // Entry 0 is for secure mode, all others for non-secure

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
extern void roll_call( core_types *present, unsigned number );

// Routine provided by chosen el3_ behaviour source file, called once, during
// initialisation, finally entering `code', at Secure EL1 with no MMU.
extern void __attribute__(( noreturn )) el3_synchronised_initialise( EL_PARAMETERS );

// Entry routine for IsambardOS at EL1
extern void __attribute__(( noreturn )) isambard_secure_el1( Core *phys_core, int number, uint64_t *present );

extern void __attribute__(( noreturn )) el3_run_at_secure_el1( EL_PARAMETERS, isambard_init_code code );

extern void __attribute__(( noreturn )) el3_run_with_mmu( EL_PARAMETERS, isambard_init_code code );
