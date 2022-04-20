
// Registers set at EL1, secure and non-secure
// Order is fixed for el3_virtual_machines.c assembler
typedef struct {
  uint64_t mair_el1;
  uint64_t sctlr_el1;

  uint64_t tcr_el1;
  uint64_t ttbr0_el1; // Core-specific, in secure mode

  uint64_t ttbr1_el1;
  uint64_t vbar_el1;

  uint64_t actlr_el1;
  uint64_t fpexc32_el2; // Placeholder

  uint64_t esr_el1;
  uint64_t far_el1;

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

#if 0
// The optimizer in gcc-10 isn't quite up to producing optimal
// code without the pairing of neighbouring register values

#define LOAD_PAIR( a, b ) asm ( "msr "#a", %["#a"]\n  msr "#b", %["#b"]" : : [a] "r" (s->a), [b] "r" (s->b) );
static inline void load_vm_state( vm_state const *s )
{
  LOAD_PAIR( mair_el1, sctlr_el1 );

  LOAD_PAIR( tcr_el1, ttbr0_el1 );

  LOAD_PAIR( ttbr1_el1, vbar_el1 );

  LOAD_PAIR( actlr_el1, fpexc32_el2 );

  LOAD_PAIR( esr_el1, far_el1 );

  LOAD_PAIR( vttbr_el2, hcr_el2 );

  LOAD_PAIR( hstr_el2, vmpidr_el2 );

  LOAD_PAIR( vpidr_el2, vtcr_el2 );

  LOAD_PAIR( dacr32_el2, contextidr_el1 );
}

#define SAVE_PAIR( a, b ) asm ( "mrs %["#a"], "#a"\n  mrs %["#b"], "#b"" : [a] "=r" (s->a), [b] "=r" (s->b) );
static inline void save_vm_state( vm_state *s )
{
  SAVE_PAIR( mair_el1, sctlr_el1 );

  SAVE_PAIR( tcr_el1, ttbr0_el1 );

  SAVE_PAIR( ttbr1_el1, vbar_el1 );

  SAVE_PAIR( actlr_el1, fpexc32_el2 );

  SAVE_PAIR( esr_el1, far_el1 );

  SAVE_PAIR( vttbr_el2, hcr_el2 );

  SAVE_PAIR( hstr_el2, vmpidr_el2 );

  SAVE_PAIR( vpidr_el2, vtcr_el2 );

  SAVE_PAIR( dacr32_el2, contextidr_el1 );
}
#endif

