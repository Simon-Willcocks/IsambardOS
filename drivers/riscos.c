/* Copyright (c) 2021 Simon Willcocks */

// Load RISCOS image from SD card, execute it in a Non-Secure environment.

#include "drivers.h"
#include "aarch64_vmsa.h"

ISAMBARD_INTERFACE( BLOCK_DEVICE )
#include "interfaces/client/BLOCK_DEVICE.h"

static const NUMBER el2_tt_address = { .r = 0x80000 };
static const NUMBER ro_address = { .r = 0x8000000 };
static const NUMBER rom_size = { .r = 6*1024*1024 }; // Should be 5, when find_and_map_memory works properly!
static const uint32_t real_rom_size = 5*1024*1024;
static const NUMBER rom_load = { .r = 0 }; // Offset into virtual machine memory (Only working on 2MB boundaries atm.)
static const NUMBER riscos_ram_size = { .r = 64*1024*1024 };

static const NUMBER disc_address = { .r = 0xe1c0 }; // RISCOS.IMG block number

ISAMBARD_INTERFACE( TRIVIAL_NUMERIC_DISPLAY )
#include "interfaces/client/TRIVIAL_NUMERIC_DISPLAY.h"
#define N( n ) NUMBER__from_integer_register( n )

TRIVIAL_NUMERIC_DISPLAY tnd = {};

static void load_guest_os( PHYSICAL_MEMORY_BLOCK riscos_memory )
{
#ifdef QEMU
  // Just writes to the VM memory when asked to check the image
#else
  PHYSICAL_MEMORY_BLOCK rom_memory;
  rom_memory = PHYSICAL_MEMORY_BLOCK__subblock( riscos_memory, rom_load, rom_size );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 910 ), PHYSICAL_MEMORY_BLOCK__physical_address( rom_memory ), N( 0xff0000ff ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 10 ), N( 0x12121212 ), N( 0xfffff0f0 ) );

  NUMBER timer0 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 100 ), ( timer0 ), N( 0xfffff0f0 ) );

  BLOCK_DEVICE emmc = BLOCK_DEVICE__get_service( "EMMC", -1 );

  BLOCK_DEVICE__read_4k_pages( emmc, PHYSICAL_MEMORY_BLOCK__duplicate_to_pass_to( emmc.r, rom_memory ), disc_address );

  NUMBER timer1 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 110 ), ( timer1 ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 120 ), N( timer1.r - timer0.r ), N( 0xfffff0f0 ) );

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 10 ), N( 0x13131313 ), N( 0xfffff0f0 ) );
#endif
}

static uint32_t software_crc32( uint8_t *p, uint32_t len )
{
  const uint32_t Polynomial = 0xEDB88320;
  uint32_t crc = ~0;
  for (unsigned c = 0; c < real_rom_size; c ++) { // Actual size of ROM
    crc ^= p[c];
    for (unsigned int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & Polynomial);
    }
  }
  return ~crc;
}

static uint32_t __attribute__(( target("+crc") )) hardware_crc32( uint8_t *p, uint32_t len )
{
  uint32_t crc = ~0;
  uint64_t *crcp = (void*) p;
  for (unsigned c = 0; c < real_rom_size/16; c ++) {
    asm ( "crc32x %w[crcout], %w[crcin], %[data0]\n\tcrc32x %w[crcout], %w[crcout], %[data1]" : [crcout] "=r" (crc) : [crcin] "r" (crc), [data0] "r" (*crcp++), [data1] "r" (*crcp++) );
  }
  return ~crc;
}

#ifdef QEMU
static void timer_thread()
{
  uint32_t *arm_code = (void *) ro_address.r;
  uint32_t count = 0;
  for (;;) {
    // Insufficient asm volatile ( "dc civac, %[va]" : : [va] "r" (&arm_code[0x100]) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 1000 ), NUMBER__from_integer_register( arm_code[0x100] ), N( 0xffff0000 | (count & 15) * 0x1020 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 1010 ), NUMBER__from_integer_register( count++ ), N( 0xffffffff ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 1020 ), NUMBER__from_integer_register( this_thread ), N( 0xffffffff ) );
    asm ( "svc 0" );
    sleep_ms( 25 );
  }
}
#endif

static bool image_is_valid()
{
#ifdef QEMU
  uint32_t *arm_code = (void *) ro_address.r;
  int i = 0;

  arm_code[i++] = 0xea00000c; // b 1f
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b
  arm_code[i++] = 0xeafffffe; // 0: b 0b

  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  arm_code[i++] = 0xe3a01000; // mov	r1, #0

  // Set the reset entry point to an infinite loop (Secure EL1 has no cache)
  arm_code[i++] = 0xe5912004; // ldr	r2, [r1, #4]
  arm_code[i++] = 0xe5812000; // str	r2, [r1]


  arm_code[i++] = 0xe3a0947f; // mov r9, #0x78000000
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  int loop = i;
                              // loop:
  arm_code[i++] = 0xe2999001; // add r9, r9, #1
  arm_code[i++] = 0xe5819400; // str	r9, [r1, #1024]	; 0x400
  uint32_t branch = 0x5afffffe - (i - loop);
  arm_code[i++] = branch; // bpl loop

  arm_code[i++] = 0xe3a0743f; // 	mov	r7, #1056964608	; 0x3f000000
  arm_code[i++] = 0xe3877602; // 	orr	r7, r7, #2097152	; 0x200000
  arm_code[i++] = 0xe3a06010; // 	mov	r6, #16
  arm_code[i++] = 0xe587601c; // 	str	r6, [r7, #28]

  branch = 0xeafffffe - i + 12;
  arm_code[i++] = branch; // b 0x30

  for (int j = 0; j < i; j++) {
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1000 ), N( j*12 + 100 ), NUMBER__from_integer_register( arm_code[j] ), N( 0xffffffff ) );
  }
  arm_code[0x100] = 0x11223344;

  static uint64_t __attribute__(( aligned( 16 ) )) stack[32];
  create_thread( timer_thread, stack + 32 );

  return true;
#else
  uint32_t crc;
  const unsigned char* rom_base = (void*) (ro_address.r + rom_load.r);

  uint32_t const expected_crc = 0x42e1de28;

  static unsigned const top = 50;

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 20 ), N( top - 10 ), NUMBER__from_integer_register( expected_crc ), N( 0xffffffff ) );

  NUMBER timer0 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 720 ), ( timer0 ), N( 0xfffff0f0 ) );

  crc = software_crc32( rom_base, real_rom_size );

  NUMBER timer1 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 730 ), ( timer1 ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 740 ), N( timer1.r - timer0.r ), N( 0xfffff0f0 ) );

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 ), N( top - 10 ), NUMBER__from_integer_register( crc ), N( 0xffffffff ) );

  timer0 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 720 ), ( timer0 ), N( 0xfffff0f0 ) );

  // 0x04C11DB7 is reversed 0xEDB88320 (So use CRC32x instructions, not CRC32Cx.)
  crc = hardware_crc32( rom_base, real_rom_size );

  timer1 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( top - 30 ), ( timer1 ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( top - 20 ), N( timer1.r - timer0.r ), N( 0xfffff0f0 ) );

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 180 ), N( top - 10 ), NUMBER__from_integer_register( crc ), N( 0xffffffff ) );

  uint32_t *arm_code = (void *) ro_address.r;
  arm_code[0x2000] = 0xeafffffe; // 0: b 0b

  return (crc == expected_crc);
#endif
}

typedef union {
  uint32_t raw;
  struct {
    uint32_t is_read:1;
    uint32_t CRm:4;
    uint32_t Rt:5;
    uint32_t CRn:4;
    uint32_t Opc1:3;
    uint32_t Opc2:3;
    uint32_t COND:4;
    uint32_t CV:1;
  };
} copro_syndrome;

#define READ_ONLY( n ) if (cp.is_read) set_partner_register( cp.Rt, n ); else asm ( "brk 1" );
#define READ_WRITE( n ) { static uint64_t v = n; if (cp.is_read) set_partner_register( cp.Rt, v ); else v = get_partner_register( cp.Rt ); }

static void cp15_access( uint32_t syndrome )
{
  copro_syndrome cp = { .raw = syndrome };

  // 0fe00241 mrc     15, 0, lr, cr0, cr0, {0}

  // The following switch statements follow the order mappings are listed in G6.3.1, etc.
  // The order is different in cp14!
  // Most of the constants have been taken from qemu-5.2.
  switch (cp.CRn) {
  case 0: // ID registers
    switch (cp.Opc1) {
    case 0:
      switch (cp.CRm) {
      case 0:
        {
        switch (cp.Opc2) {
        case 0:
          READ_ONLY( 0x410fc075 ); // midr
          break;
        case 5:
          READ_ONLY( 0x80000f00 ); // mpidr
          break;

        default: asm ( "brk 1" );
        }
        }
        break;
      default: asm ( "brk 1" );
      }
      break;
    default: asm ( "brk 1" );
    }
    break;
  case 1: // System control registers
    switch (cp.Opc1) {
    case 0:
      switch (cp.CRm) {
      case 0:
        {
        switch (cp.Opc2) {
        case 0:
          READ_WRITE( 0x701fe00a ); // ccsidr, what does a write do?
          break;

        default: asm ( "brk 1" );
        }
        }
        break;
      default: asm ( "brk 1" );
      }
      break;
    default: asm ( "brk 1" );
    }
    break;
  case 2: // Memory system control registers
  case 3: // Memory system control registers
  case 4: // GIC, system control registers, Debug exception registers
  case 5: // Memory system fault registers
  case 6: // Memory system fault registers
  case 7: // Cache maintenance, address translations, legacy operations
    switch (cp.Opc1) {
    case 0:
      switch (cp.CRm) {
      case 5:
        {
        switch (cp.Opc2) {
        case 4:
          // CP15ISB - isb, why?
          break;
        default: asm ( "brk 1" );
        }
        }
        break;
      default: asm ( "brk 1" );
      }
      break;
    default: asm ( "brk 1" );
    }
    break;
  case 8: // TLB maintenance operations
  case 9: // Performance monitors
  case 10: // Memory mapping registers and TLB operations
  case 11: // Reserved for DMA operations for TCM access
  case 12: // System control registers, GIC System registers *
  case 13: // Process, Context, and Thread ID registers
  case 14: // Generic Timer registers *, Performance Monitors registers
  case 15: // IMPLEMENTATION DEFINED registers
  default: asm ( "brk 1" );
  }
}

static void cp14_access( uint32_t syndrome )
{
  copro_syndrome cp = { .raw = syndrome };

  // 0fe00241 mrc     15, 0, lr, cr0, cr0, {0}

  // The following switch statements follow the order mappings are listed in G6.2.1, etc.
  switch (cp.Opc1) {
  case 0:
    switch (cp.CRn) {
    case 0:
      switch (cp.Opc2) {
      case 0:
        switch (cp.CRm) {
        case 0:
          break;
        default: asm ( "brk 1" );
        }
        break;
      default: asm ( "brk 1" );
      }
      break;
    default: asm ( "brk 1" );
    }
    break;
  default: asm ( "brk 1" );
  }
}

static void cp15_access_RR( uint32_t syndrome ) { asm ( "brk 1" ); }
static void ldstc( uint32_t syndrome ) { asm ( "brk 1" ); }
static void fpaccess( uint32_t syndrome ) { asm ( "brk 1" ); }
static void vmrs_access( uint32_t syndrome ) { asm ( "brk 1" ); }
static void pointer_authentication( uint32_t syndrome ) { asm ( "brk 1" ); }
static void cp14_access_RR( uint32_t syndrome ) { asm ( "brk 1" ); }
static void illegal_execution_state( uint32_t syndrome ) { asm ( "brk 1" ); }
static void hvc32( uint32_t syndrome ) { asm ( "brk 1" ); }
static void smc32( uint32_t syndrome ) { asm ( "brk 1" ); }

uint64_t vm_handler( uint64_t pc, uint64_t syndrome, uint64_t fault_address, uint64_t intermediate_physical_address )
{
  static int count = 0;
  uint64_t new_pc = pc; // Retry instruction by default

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 200 ), N( syndrome ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 210 ), N( fault_address ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 220 ), N( intermediate_physical_address ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 230 ), N( pc ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 240 ), N( count++ ), N( 0xfffff0f0 ) );
asm ( "svc 0" );
  switch (syndrome >> 26) {
  case 0b000000: asm ( "brk 0b000000" ); break; // Never happens or is unimplemented
  case 0b000001: wait_until_woken(); break; // WFI or WFE
  case 0b000010: asm ( "brk 0b000010" ); break; // Never happens or is unimplemented
  case 0b000011: cp15_access( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b000100: cp15_access_RR( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b000101: cp14_access( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b000110: ldstc( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b000111: fpaccess( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b001000: vmrs_access( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b001001: pointer_authentication( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b001010: asm ( "brk 0b001010" ); break; // Never happens or is unimplemented
  case 0b001011: asm ( "brk 0b001011" ); break; // Never happens or is unimplemented
  case 0b001100: cp14_access_RR( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b001101: asm ( "brk 0b001101" ); break; // Never happens or is unimplemented
  case 0b001110: illegal_execution_state( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b001111: asm ( "brk 0b001111" ); break; // Never happens or is unimplemented
  case 0b010000: asm ( "brk 0b010000" ); break; // Never happens or is unimplemented
  case 0b010001: asm ( "brk 0b010001" ); break; // Never happens or is unimplemented
  case 0b010010: hvc32( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b010011: smc32( syndrome & 0x1fffff ); new_pc += 4; break;
  case 0b010100: asm ( "brk 0b010100" ); break; // Never happens or is unimplemented
  case 0b010101: asm ( "brk 0b010101" ); break; // Never happens or is unimplemented
  case 0b010110: asm ( "brk 0b010110" ); break; // Never happens or is unimplemented
  case 0b010111: asm ( "brk 0b010111" ); break; // Never happens or is unimplemented
  case 0b011000: asm ( "brk 0b011000" ); break; // Never happens or is unimplemented
  case 0b011001: asm ( "brk 0b011001" ); break; // Never happens or is unimplemented
  case 0b011010: asm ( "brk 0b011010" ); break; // Never happens or is unimplemented
  case 0b011011: asm ( "brk 0b011011" ); break; // Never happens or is unimplemented
  case 0b011100: asm ( "brk 0b011100" ); break; // Never happens or is unimplemented
  case 0b011101: asm ( "brk 0b011101" ); break; // Never happens or is unimplemented
  case 0b011110: asm ( "brk 0b011110" ); break; // Never happens or is unimplemented
  case 0b011111: asm ( "brk 0b011111" ); break; // Never happens or is unimplemented
  case 0b100000: asm ( "brk 0b100000" ); break; // Never happens or is unimplemented
  case 0b100001: asm ( "brk 0b100001" ); break; // Never happens or is unimplemented
  case 0b100010: asm ( "brk 0b100010" ); break; // Never happens or is unimplemented
  case 0b100011: asm ( "brk 0b100011" ); break; // Never happens or is unimplemented
  case 0b100100: asm ( "brk 0b100100" ); break; // Never happens or is unimplemented
  case 0b100101: asm ( "brk 0b100101" ); break; // Never happens or is unimplemented
  case 0b100110: asm ( "brk 0b100110" ); break; // Never happens or is unimplemented
  case 0b100111: asm ( "brk 0b100111" ); break; // Never happens or is unimplemented
  case 0b101000: asm ( "brk 0b101000" ); break; // Never happens or is unimplemented
  case 0b101001: asm ( "brk 0b101001" ); break; // Never happens or is unimplemented
  case 0b101010: asm ( "brk 0b101010" ); break; // Never happens or is unimplemented
  case 0b101011: asm ( "brk 0b101011" ); break; // Never happens or is unimplemented
  case 0b101100: asm ( "brk 0b101100" ); break; // Never happens or is unimplemented
  case 0b101101: asm ( "brk 0b101101" ); break; // Never happens or is unimplemented
  case 0b101110: asm ( "brk 0b101110" ); break; // Never happens or is unimplemented
  case 0b101111: asm ( "brk 0b101111" ); break; // Never happens or is unimplemented
  case 0b110000: asm ( "brk 0b110000" ); break; // Never happens or is unimplemented
  case 0b110001: asm ( "brk 0b110001" ); break; // Never happens or is unimplemented
  case 0b110010: asm ( "brk 0b110010" ); break; // Never happens or is unimplemented
  case 0b110011: asm ( "brk 0b110011" ); break; // Never happens or is unimplemented
  case 0b110100: asm ( "brk 0b110100" ); break; // Never happens or is unimplemented
  case 0b110101: asm ( "brk 0b110101" ); break; // Never happens or is unimplemented
  case 0b110110: asm ( "brk 0b110110" ); break; // Never happens or is unimplemented
  case 0b110111: asm ( "brk 0b110111" ); break; // Never happens or is unimplemented
  case 0b111000: asm ( "brk 0b111000" ); break; // Never happens or is unimplemented
  case 0b111001: asm ( "brk 0b111001" ); break; // Never happens or is unimplemented
  case 0b111010: asm ( "brk 0b111010" ); break; // Never happens or is unimplemented
  case 0b111011: asm ( "brk 0b111011" ); break; // Never happens or is unimplemented
  case 0b111100: asm ( "brk 0b111100" ); break; // Never happens or is unimplemented
  case 0b111101: asm ( "brk 0b111101" ); break; // Never happens or is unimplemented
  case 0b111110: asm ( "brk 0b111110" ); break; // Never happens or is unimplemented
  case 0b111111: asm ( "brk 0b111111" ); break; // Never happens or is unimplemented
  }

  return new_pc;
}

void entry()
{
  tnd = TRIVIAL_NUMERIC_DISPLAY__get_service( "Trivial Numeric Display", -1 );

  PHYSICAL_MEMORY_BLOCK riscos_memory;
  riscos_memory = SYSTEM__allocate_memory( system, riscos_ram_size );
  if (riscos_memory.r == 0) {
    asm ( "brk 2" );
  }

  load_guest_os( riscos_memory );

  DRIVER_SYSTEM__map_at( driver_system(), riscos_memory, ro_address );

  if (image_is_valid()) {
    // Establish a translation table mapping the VM "physical" addresses to real memory
    static const NUMBER tt_size = { .r = 4096 };
    PHYSICAL_MEMORY_BLOCK el2_tt = SYSTEM__allocate_memory( system, tt_size );
    DRIVER_SYSTEM__map_at( driver_system(), el2_tt, el2_tt_address );

    uint32_t base = PHYSICAL_MEMORY_BLOCK__physical_address( riscos_memory ).r;

    Aarch64_VMSA_entry *tt = (void*) el2_tt_address.r;
    for (int i = 0; i < 512; i++) {
      tt[i] = Aarch64_VMSA_invalid;
    }

    for (int i = 0; i < 32; i++) {
      Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( base + (i << 21) );
      entry = Aarch64_VMSA_write_back_memory( entry );
      entry.raw |= (1 <<10); // AF
      entry = Aarch64_VMSA_L2_rwx( entry );
      tt[i] = entry;
    }
    DRIVER_SYSTEM__make_partner_thread( driver_system(), PHYSICAL_MEMORY_BLOCK__duplicate_to_pass_to( driver_system().r, el2_tt ) );
    uint64_t next_pc = 0;
    for (;;) {
      next_pc = switch_to_partner( vm_handler, next_pc );
sleep_ms( 4000 );
    }
  }
}

