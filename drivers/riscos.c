/* Copyright (c) 2021 Simon Willcocks */

// Load RISCOS image from SD card, execute it in a Non-Secure environment.

#include "drivers.h"
#include "aarch64_vmsa.h"

#define QEMU

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
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 800 ), N( 200 ), N( syndrome ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 800 ), N( 210 ), N( fa ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 800 ), N( 220 ), N( ipa ), N( 0xfffff0f0 ) );
  asm ( "svc 0" );
}

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
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1000 ), N( 1000 ), NUMBER__from_integer_register( arm_code[0x100] ), N( 0xffffffff ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1000 ), N( 1010 ), NUMBER__from_integer_register( count++ ), N( 0xffffffff ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1000 ), N( 1020 ), NUMBER__from_integer_register( this_thread ), N( 0xffffffff ) );
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


  //arm_code[i++] = 0xe3a0947f; // mov r9, #0x7f000000
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
  return (crc == expected_crc);
#endif
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
    int count = 0;
    for (;;) {
      TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 750 ), N( 250 ), N( count++ ), N( 0xffffff00 ) );
      asm ( "svc 0" );
      switch_to_partner( vm_exception_handler );
    }
  }
}

