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

void vm_exception_handler( uint32_t syndrome, uint64_t fa, uint64_t ipa )
{
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 400 ), N( 200 ), N( syndrome ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 400 ), N( 210 ), N( fa ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 400 ), N( 220 ), N( ipa ), N( 0xfffff0f0 ) );
  asm ( "svc 0" );
}

void __attribute__(( target("+crc") )) entry()
{
  tnd = TRIVIAL_NUMERIC_DISPLAY__get_service( "Trivial Numeric Display", -1 );

  PHYSICAL_MEMORY_BLOCK riscos_memory;
  riscos_memory = SYSTEM__allocate_memory( system, riscos_ram_size );
  if (riscos_memory.r == 0) {
    asm ( "brk 2" );
  }

#ifndef QEMU
#define LOAD_AND_RUN_RISC_OS
#endif

#ifdef LOAD_AND_RUN_RISC_OS
  PHYSICAL_MEMORY_BLOCK rom_memory;
  rom_memory = PHYSICAL_MEMORY_BLOCK__subblock( riscos_memory, rom_load, rom_size );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 910 ), PHYSICAL_MEMORY_BLOCK__physical_address( rom_memory ), N( 0xff0000ff ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 10 ), N( 0x12121212 ), N( 0xfffff0f0 ) );

  NUMBER timer0 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 100 ), ( timer0 ), N( 0xfffff0f0 ) );
#ifndef QEMU
  BLOCK_DEVICE emmc = BLOCK_DEVICE__get_service( "EMMC", -1 );

  BLOCK_DEVICE__read_4k_pages( emmc, PHYSICAL_MEMORY_BLOCK__duplicate_to_pass_to( emmc.r, rom_memory ), disc_address );
#endif

  NUMBER timer1 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 110 ), ( timer1 ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 120 ), N( timer1.r - timer0.r ), N( 0xfffff0f0 ) );

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 10 ), N( 0x13131313 ), N( 0xfffff0f0 ) );

  DRIVER_SYSTEM__map_at( driver_system(), riscos_memory, ro_address );

#ifdef QEMU
  uint32_t *arm_code = (void *) ro_address.r;
  arm_code[0] = 0xeafffffe; // Simple loop. Also a place where the image can be loaded in gdb
#endif

  const uint32_t Polynomial = 0xEDB88320;
  uint32_t crc = 0xFFFFFFFF;
  const unsigned char* current = (void*) (ro_address.r + rom_load.r);

  uint32_t const expected_crc = 0x42e1de28;

  static unsigned const top = 50;

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 20 ), N( top - 10 ), NUMBER__from_integer_register( expected_crc ), N( 0xffffffff ) );

  timer0 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 720 ), ( timer0 ), N( 0xfffff0f0 ) );

  for (unsigned c = 0; c < real_rom_size; c ++) { // Actual size of ROM
    crc ^= current[c];
    for (unsigned int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & Polynomial);
    }
  }

  timer1 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 730 ), ( timer1 ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 740 ), N( timer1.r - timer0.r ), N( 0xfffff0f0 ) );

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 ), N( top - 10 ), NUMBER__from_integer_register( ~crc ), N( 0xffffffff ) );

  timer0 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 720 ), ( timer0 ), N( 0xfffff0f0 ) );

  // 0x04C11DB7 is reversed 0xEDB88320 (So use CRC32x instructions, not CRC32Cx.)
  crc = 0xffffffff;
  uint64_t *crcp = (void*) current;
  for (unsigned c = 0; c < real_rom_size/16; c ++) {
    asm ( "crc32x %w[crcout], %w[crcin], %[data0]\n\tcrc32x %w[crcout], %w[crcout], %[data1]" : [crcout] "=r" (crc) : [crcin] "r" (crc), [data0] "r" (*crcp++), [data1] "r" (*crcp++) );
  }


  timer1 = DRIVER_SYSTEM__get_ms_timer_ticks( driver_system() );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( top - 30 ), ( timer1 ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( top - 20 ), N( timer1.r - timer0.r ), N( 0xfffff0f0 ) );

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 180 ), N( top - 10 ), NUMBER__from_integer_register( ~crc ), N( 0xffffffff ) );

  if (~crc == expected_crc) {
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
      entry = Aarch64_VMSA_el0_rwx( entry );
      tt[i] = entry;
    }

    DRIVER_SYSTEM__make_partner_thread( driver_system(), PHYSICAL_MEMORY_BLOCK__duplicate_to_pass_to( driver_system().r, el2_tt ) );
    int count = 0;
    for (;;) {
      asm ( "svc 0" );
      switch_to_partner( vm_exception_handler );
      TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 ), N( 200 ), N( count++ ), N( 0xffffffff ) );
    }
  }

  static unsigned const lines = 80;
  static unsigned max = real_rom_size;
  int sleeptime = 4000;

  for (unsigned line = 0; line < real_rom_size - lines * 64; line += 64) {
    for (int l = 0; l < lines; l++) {
      uint32_t off = (line + 64 * l) % max;
      TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 20 ), N( top + 10 * l ), NUMBER__from_integer_register( off ), N( 0xffffffff ) );

      uint32_t *p = (void*) (ro_address.r + off);
      for (int i = 0; i < 16; i++) {
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 + i * 80 ), N( top + 10 * l ), N( p[i] ), N( 0xffffffff ) );
      }
    }
    asm ( "svc 0" );
    sleep_ms( sleeptime );
    sleeptime = 100;
  }
#else
  static const NUMBER tt_size = { .r = 4096 };
  DRIVER_SYSTEM__map_at( driver_system(), riscos_memory, ro_address );
  uint32_t base = PHYSICAL_MEMORY_BLOCK__physical_address( riscos_memory ).r;

  PHYSICAL_MEMORY_BLOCK el2_tt = SYSTEM__allocate_memory( system, tt_size );
  DRIVER_SYSTEM__map_at( driver_system(), el2_tt, el2_tt_address );

  Aarch64_VMSA_entry *tt = (void*) el2_tt_address.r;
  for (int i = 0; i < 512; i++) {
    tt[i] = Aarch64_VMSA_invalid;
  }

  for (int i = 0; i < 32; i++) {
    Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( base + (i << 21) );
    entry = Aarch64_VMSA_write_back_memory( entry );
    entry.raw |= (1 <<10); // AF
    entry = Aarch64_VMSA_el0_rwx( entry );
    tt[i] = entry;
  }

  uint32_t *arm_code = (void *) ro_address.r;
  arm_code[0] = 0xe3a08040; // mov r8, #64
  arm_code[1] = 0xe3a05006; // mov r5, #6
  arm_code[2] = 0xe3a0143f; // 	mov	r1, #1056964608	; 0x3f000000
  arm_code[3] = 0xe3811602; // 	orr	r1, r1, #2097152	; 0x200000
  arm_code[4] = 0xe3a02010; // 	mov	r2, #16
  arm_code[5] = 0xe581201c; // 	str	r2, [r1, #28]

  arm_code[6] = 0xee10ef10; // 0xeafffffe;
  arm_code[7] = 0xeafffffe;
  arm_code[8] = 0xee10ef10; // 0xeafffffe;

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 ), N( 100 ), N( 0xfeedf00d ), N( 0xffffffff ) );
  DRIVER_SYSTEM__make_partner_thread( driver_system(), PHYSICAL_MEMORY_BLOCK__duplicate_to_pass_to( driver_system().r, el2_tt ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 ), N( 110 ), N( 0xfeedf00d ), N( 0xffffffff ) );
  int count = 0;
  for (;;) {
    asm ( "svc 0" );
    switch_to_partner( vm_exception_handler );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 ), N( 200 ), N( count++ ), N( 0xffffffff ) );
  }
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 100 ), N( 120 ), N( 0xfeedf00d ), N( 0xffffffff ) );
#endif
}

