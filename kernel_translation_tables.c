/* Copyright (c) Simon Willcocks 2021 */

// Kernel translation tables, may be shared between all the cores.
// kernel_tt_l2 can be used as level 2 table for all ELs, but Secure EL1 will
// map it to high memory, leaving low memory to EL0.
//
// Since the memory appears at different locations, offsets from _start should
// be used to store "pointers" to kernel structures.
//
// The virtual memory map will not include the drivers

#include "kernel.h"
#include "kernel_translation_tables.h"

Aarch64_VMSA_entry __attribute__(( aligned( 4096 ) )) kernel_tt_l3[512] = { 0 };
// 16 entries of 2MB
Aarch64_VMSA_entry __attribute__(( aligned( 256 ) )) kernel_tt_l2[16] = { 0 };

typedef union {
  uint64_t raw;
  struct __attribute__(( packed )) {
    uint64_t t0sz:6;
    uint64_t res1:1;
    uint64_t translation_table_walk_disable0:1;
    uint64_t inner_cache0:2;
    uint64_t outer_cache0:2;
    uint64_t shareable0:2;
    uint64_t granule0:2;
    uint64_t t1sz:6;
    uint64_t asid_definer:1;
    uint64_t translation_table_walk_disable1:1;
    uint64_t inner_cache1:2;
    uint64_t outer_cache1:2;
    uint64_t shareable1:2;
    uint64_t granule1:2;
    uint64_t intermediate_physical_address_size:3;
    uint64_t res2:1;
    uint64_t large_asid:1;
    uint64_t top_byte_ignored0:1;
    uint64_t top_byte_ignored1:1;
    uint64_t hardware_access_flag:1;
    uint64_t hardware_dirty_state:1;
    uint64_t hierarchical_permission_disable0:1;
    uint64_t hierarchical_permission_disable1:1;
    uint64_t HWU059:1;
    uint64_t HWU060:1;
    uint64_t HWU061:1;
    uint64_t HWU062:1;
    uint64_t HWU159:1;
    uint64_t HWU160:1;
    uint64_t HWU161:1;
    uint64_t HWU162:1;
    uint64_t res3:2;
    uint64_t NFD0:1;
    uint64_t NFD1:1;
    uint64_t res4:9;
  };
} TCR1;


// From ld.script
extern uint8_t at_ro_start;
extern uint8_t at_rw_start;
extern uint8_t at_rw_end;
extern uint8_t initial_first_free_page;

void assign_kernel_entry( Aarch64_VMSA_entry entry, uint64_t kernel_page )
{
  // May only add to the kernel's memory, not replace or remove (for now)
  if (kernel_page >= 512 || kernel_tt_l3[kernel_page].raw != Aarch64_VMSA_invalid.raw) {
    asm volatile( "wfi" );
    for (;;) {} // FIXME
  }
  kernel_tt_l3[kernel_page] = entry;
}

void initialise_shared_isambard_kernel_tables( Core *core0, int cores )
{
  if (kernel_tt_l2[0].raw != 0) return; // Already been called (allows for EL3, EL1, or both, to call

  // 2MB chunk, in 4k pages
  kernel_tt_l2[0] = Aarch64_VMSA_subtable_at( kernel_tt_l3 );

  integer_register writable_area = &at_rw_start - (uint8_t*) 0;
  integer_register writable_area_end = &at_rw_end - (uint8_t*) 0;

  // Level 3 lookup tables, so bit 0 -> valid, bit 1 -> not reserved
  // Level 0, 1, 2 lookups have bit 0 -> valid, bit 1 -> table (not block)
  // Confusing when you're thinking of blocks as chunks of memory!

  // First, the kernel executable code and (from here on) constants
  for (integer_register i = 0; (i << 12) < writable_area; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( (i << 12) );
    entry = Aarch64_VMSA_priv_r_x( entry );
    entry.access_flag = 1; // Don't want to be notified when accessed
    entry.shareability = 3; // Inner shareable
    entry = Aarch64_VMSA_global( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    kernel_tt_l3[i] = entry;
  }

  // Second, the writable memory
  for (integer_register i = writable_area >> 12; (i << 12) < writable_area_end; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( (i << 12) );
    entry = Aarch64_VMSA_priv_rw_( entry, 3 );
    entry.access_flag = 1; // Don't want to be notified when accessed
    entry.shareability = 3; // Inner shareable
    entry = Aarch64_VMSA_global( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    kernel_tt_l3[i] = entry;
  }

  // Third, the Core structures, moved to just above the writable sections.
  // When the MMU is initialised, the stack of the core will move by the size of all the drivers
  integer_register virtual_cores_area_end = (writable_area_end + cores * sizeof( Core ));
  for (integer_register i = (writable_area_end >> 12); (i << 12) < virtual_cores_area_end; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_page_at( ((integer_register) core0) + (i << 12) - writable_area_end);
    entry = Aarch64_VMSA_priv_rw_( entry, 3 );
    entry.access_flag = 1; // Don't want to be notified when accessed
    entry.shareability = 3; // Inner shareable
    entry = Aarch64_VMSA_global( entry );
    entry = Aarch64_VMSA_write_back_memory( entry );
    kernel_tt_l3[i] = entry;
  }
}

void *himem_address( void *offset )
{
  return ((uint8_t*) offset) + himem_offset;
}

void switch_to_running_in_high_memory( Core *phys_core, void (*himem_code)( Core *core ) )
{
  uint64_t virtual_stack_offset = &initial_first_free_page - &at_rw_end;

  phys_core->physical_address = phys_core;
  Core *va = (void*) (((uint8_t*) phys_core) - virtual_stack_offset);
  phys_core->low_virtual_address = va;

  // Trivial low-memory mapping, rwx for 1GB
  Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( 0 );
  entry = Aarch64_VMSA_priv_rwx( entry );
  entry = Aarch64_VMSA_outer_write_through_memory( entry );
  entry = Aarch64_VMSA_not_global( entry );
  entry.access_flag = 1;

  phys_core->core_tt_l1[0] = entry;

  // This will be used by all EL1 translation tables (what happens if non-secure EL1 sets it? FIXME
  asm volatile ( "\tmsr MAIR_EL1, %[bits]":: [bits] "r" (Aarch64_VMSA_Isambard_memory_attributes) );

  const TCR1 tcr = { .t0sz = 30,        // 16GB
                    .inner_cache0 = 1,
                    .outer_cache0 = 1,
                    .shareable0 = 0,    // Non-shareable (each core has its own translation tables)
                    .granule0 = 0,      // 4k
		    .t1sz = 39,         // 32MB (kernel structures, and this code)
                    .inner_cache1 = 1,
                    .outer_cache1 = 1,
                    .shareable1 = 3,
                    .granule1 = 0,      // 4k
                    .intermediate_physical_address_size = 0, // 32 bit
                    .large_asid = 1 };

  register uint64_t tmp;

  asm volatile ( "\tmsr TCR_EL1, %[bits]":: [bits] "r" (tcr.raw) );
  vm[0].tcr_el1 = tcr.raw;
  vm[0].ttbr1_el1 = (uint64_t) kernel_tt_l2;
  asm volatile ( "\tmsr TTBR1_EL1, %[table]" :: [table] "r" (kernel_tt_l2) );
  asm volatile ( "\tmsr TTBR0_EL1, %[table]" :: [table] "r" (&phys_core->core_tt_l1) );
  asm volatile ( "\tmrs %[sctlr], SCTLR_EL1"
               "\n\torr %[sctlr], %[sctlr], #(1<<0)"      // M
               "\n\torr %[sctlr], %[sctlr], #(1<<2)"      // C
               "\n\torr %[sctlr], %[sctlr], #(1<<12)"     // I
               "\n\torr %[sctlr], %[sctlr], #(1<<14)"     // DZE Allow EL0 to use DC ZVA (for fast bzero)
               "\n\torr %[sctlr], %[sctlr], #(1<<26)"     // UCI Allow EL0 to use cache maintenance operations
               "\n\tmsr SCTLR_EL1, %[sctlr]"

	       // Running in low memory, correct stack pointer (in Core structure)
               "\n\tsub sp, sp, %[offset]"

	       // Running in low memory, jump to high memory
               "\n\tadr %[tmp], continue"
               "\n\tadd %[tmp], %[tmp], %[himem]"
               "\n\tbr %[tmp]"
               "\ncontinue:"
	       // Running in high memory, correct stack pointer (in Core structure)
               "\n\tadd sp, sp, %[himem]"
	       // Running in high memory
	       : [tmp] "=&r" (tmp)
	       , [sctlr] "=&r" (vm[0].sctlr_el1)
	       : [offset] "r" (virtual_stack_offset)
	       , [himem] "r" (himem_offset) );

  Core *hicore = himem_address( va );

  // Forget about the low-memory mapping
  // Break-before-make, see:
  // D4 The AArch64 Virtual Memory System Architecture
  // D4.10 TLB maintenance requirements and the TLB maintenance instructions p. 2209
  hicore->core_tt_l1[0] = Aarch64_VMSA_invalid;
  asm volatile ( "\tdsb sy" );
  // asm volatile ( "\ttlbi VAAE1IS, xzr" );
  asm volatile ( "\tdsb sy" );
/*
  extern Aarch64_VMSA_entry kernel_tt_l2[];

  entry = Aarch64_VMSA_block_at( 0x3f200000 );
  entry = Aarch64_VMSA_priv_rwx( entry );
  entry = Aarch64_VMSA_device_memory( entry );
  kernel_tt_l2[12] = entry;
  asm ( "dsb sy" );

extern void led_off( uint64_t base );
extern void led_on( uint64_t base );
extern void led_init( uint64_t base );

  led_init( himem_address( 12 << 21 ) );
  led_off( himem_address( 12 << 21 ) );

  for (int i = 0; i < 4; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( 0x0e400000 + (i << 21) );
    entry = Aarch64_VMSA_priv_rwx( entry );
    entry = Aarch64_VMSA_outer_write_through_memory( entry );
    kernel_tt_l2[8] = entry;
  }
*/
  himem_code( hicore );
}

void set_asid( Core *core, uint64_t asid )
{
  if (asid >= 0x10000) {
    asm ( "svc 0x1" );
  }
  asm volatile ( "\tmsr TTBR0_EL1, %[table]" :: [table] "r" ((asid << 48) | (integer_register) &core->physical_address->core_tt_l1) );
}

void el3_prepare_el2_for_entry( Core *core )
{
  // VA and PA match
  asm volatile ( "\tmsr TTBR0_EL2, %[table]" :: [table] "r" (kernel_tt_l2) );
  asm volatile ( "\tmsr MAIR_EL2, %[bits]":: [bits] "r" (Aarch64_VMSA_Isambard_memory_attributes) );
  //asm volatile ( "\tmsr TCR_EL2, %[bits]":: [bits] "r" (0x80003520ull) );
  asm volatile ( "\tmsr TCR_EL2, %[bits]":: [bits] "r" (0x80003527ull) );
  asm volatile ( "\tmsr SP_EL2, %[stack]":: [stack] "r" ((&core->el2_stack)+1) );

  //                   3         2         1         0
  //                  10987654321098765432109876543210
  uint32_t sctlr2 = 0b00110000110001010001100000111111;
  // Some or all RW fields of this register have defined reset values. These apply
  // only if the PE resets into EL2 using AArch64. Otherwise, RW fields in this
  // register reset to architecturally UNKNOWN values. (Don't read-modify-write!)
  sctlr2 |= (1 << 12) | (1 << 3) | (1 << 1) | (1 << 0);
  asm volatile ( "\tmsr SCTLR_EL2, %[bits]\n" : : [bits] "r" (sctlr2) );
}

void __attribute__(( noreturn )) el3_run_at_secure_el1( Core *core, int number, uint64_t *present, isambard_init_code code )
{
  // Entering EL1 *without* the EL1 MMU enabled, but *with* MMU at EL3
  asm volatile ( "\tmsr sp_el1, %[SP_EL1]"
               "\n\tmov sp, %[SP_EL3]"
               "\n\tmsr spsr_el3, %[PSR]"
               "\n\tmsr elr_el3, %[EL1]"
               "\n\tmov x0, %[CORE]"
               "\n\tmov x1, %[NUMBER]"
               "\n\tmov x2, %[PRESENT]"
               "\n\teret"
               :
               : [SP_EL1] "r" (&core->physical_address->core)
               , [SP_EL3] "r" ((&core->el3_stack)+1)
               , [EL1] "r" (code)
               , [CORE] "r" (core->physical_address)
               , [NUMBER] "r" (number)
               , [PRESENT] "r" (present)
               , [PSR] "r" (0xc5) // EL1, using SP_EL1, interrupts disabled
               : "x0", "x1", "x2" );

  __builtin_unreachable();
}

typedef union {
  uint32_t raw;
  struct __attribute__(( packed )) {
    uint32_t t0sz:6;
    uint32_t res1:2;
    uint32_t inner_cache:2;
    uint32_t outer_cache:2;
    uint32_t shareable:2;
    uint32_t granule:2;
    uint32_t physical_size:3;
    uint32_t res2:1;
    uint32_t top_byte_ignored:1;
    uint32_t hardware_access_flag:1;
    uint32_t hardware_dirty_state:1;
    uint32_t res3:1;
    uint32_t hierarchical_permission_disable:1;
    uint32_t HWU059:1;
    uint32_t HWU060:1;
    uint32_t HWU061:1;
    uint32_t HWU062:1;
    uint32_t res4:2;
    uint32_t res5:1;
  };
} TCR3;

void __attribute__(( noreturn )) el3_run_with_mmu( EL_PARAMETERS, isambard_init_code code )
{
  uint64_t virtual_stack_offset = &initial_first_free_page - &at_rw_end;

  register uint64_t tmp;
  const TCR3 tcr = { .t0sz = 39, // 32MB
                     .inner_cache = 1,
                     .outer_cache = 1,
                     .shareable = 3,
                     .granule = 0,
                     .res5 = 1 };
  asm volatile ( "\tmsr TCR_EL3, %[bits]":: [bits] "r" (tcr.raw) );
  asm volatile ( "\tmsr MAIR_EL3, %[bits]":: [bits] "r" (Aarch64_VMSA_Isambard_memory_attributes) );
  asm volatile ( "\tmsr TTBR0_EL3, %[table]" :: [table] "r" (kernel_tt_l2) );
  asm volatile ( "\tmrs %[bits], SCTLR_EL3"
               "\n\torr %[bits], %[bits], #(1<<0)"      // M
               "\n\torr %[bits], %[bits], #(1<<2)"      // C
               "\n\torr %[bits], %[bits], #(1<<12)"     // I
               "\n\tmsr SCTLR_EL3, %[bits]" : [bits] "=&r" (tmp) );

  // No need to do anything clever to keep code running, it stays mapped at the same location. The
  // stack and core will appear to move, so update the stack pointer before C can access it.
  // In fact, reset the stack pointer to the top of the stack, we're never going to return.
  asm volatile ( "isb"
             "\n\tdsb sy"
             "\n\tTLBI ALLE1"
             "\n\tTLBI ALLE3"
             "\n\tisb"
             "\n\tdsb sy"
             "\n\tsub %[core], %[core_in], %[offset]"
             "\n\tadd sp, %[core], %[stack_offset]"
             : [core] "=&r" (core)
             : [core_in] "r" (core)
             , [offset] "r" (virtual_stack_offset)
             , [stack_offset] "r" (&((Core*)0)->core) );
  // EL3 code now only has access to 32MB of memory.

  code( EL_ARGUMENTS );

  __builtin_unreachable();
}
