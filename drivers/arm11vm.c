/* Copyright (c) 2021 Simon Willcocks */

// Load RISCOS image from SD card, execute it in a Non-Secure environment.

// #define QEMU test code fails, at the moment, better luck with RISC OS? Yes!

#include "drivers.h"
#include "aarch64_vmsa.h"
#include "exclusive.h"

ISAMBARD_INTERFACE( BLOCK_DEVICE )
#include "interfaces/client/BLOCK_DEVICE.h"

static const NUMBER el2_tt_address = { .r = 0x80000 };
static const NUMBER ro_address = { .r = 0x8000000 };
static const NUMBER rom_size = { .r = 6*1024*1024 }; // Should be 5, when find_and_map_memory works properly!
static const uint32_t real_rom_size = 5*1024*1024;
static const NUMBER rom_load = { .r = 0 }; // Offset into virtual machine memory (Only working on 2MB boundaries atm.)
static const NUMBER riscos_ram_size = { .r = 64*1024*1024 };

static const NUMBER disc_address = { .r = 0x363c0 }; // RISCOS.IMG block number
static uint32_t const expected_crc = 0x0c84b58f;

static uint64_t vm_memory_base = 0;

// Just for debug
uint32_t ttbr0_el1 = 0;

PHYSICAL_MEMORY_BLOCK riscos_memory;

ISAMBARD_INTERFACE( TRIVIAL_NUMERIC_DISPLAY )
#include "interfaces/client/TRIVIAL_NUMERIC_DISPLAY.h"
#define N( n ) NUMBER__from_integer_register( n )

TRIVIAL_NUMERIC_DISPLAY tnd = {};

void show_state()
{
  for (int i = 0; i < 34; i++) {
    uint64_t r;
    asm ( "mov x0, %[n]\nsvc 7\nmov %[r], x0" : [r] "=&r" (r) : [n] "r" (i) : "x0" );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 260+11*i ), N( r ), N( 0xffff8080 ) );
  }
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

static uint32_t software_crc32( const uint8_t *p, uint32_t len )
{
  const uint32_t Polynomial = 0xEDB88320;
  uint32_t crc = ~0;
  for (unsigned c = 0; c < len; c ++) { // Actual size of ROM
    crc ^= p[c];
    for (unsigned int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & Polynomial);
    }
  }
  return ~crc;
}

static uint32_t __attribute__(( target("+crc") )) hardware_crc32( const uint8_t *p, uint32_t len )
{
  uint32_t crc = ~0;
  uint64_t *crcp = (void*) p;
  for (unsigned c = 0; c < len/16; c ++) {
    asm ( "crc32x %w[crcout], %w[crcin], %[data0]\n\tcrc32x %w[crcout], %w[crcout], %[data1]" : [crcout] "=r" (crc) : [crcin] "r" (crc), [data0] "r" (*crcp++), [data1] "r" (*crcp++) );
  }
  return ~crc;
}

#ifdef QEMU
static void qemu_timer_thread()
{
  uint32_t *arm_code = (void *) ro_address.r;
  uint32_t count = 0;
  for (;;) {
    uint32_t colour = 0xff00ff00;
    if (++count == 100) {
    count = 0;
    for (int i = 0; i < 35; i++) {
      uint64_t n = 0x44444444;
      asm ( "mov x0, %[n]\nsvc 7\nmov %[r], x0" : [r] "=&r" (n) : [n] "r" (i) : "x0" );
      TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 600 + 10*i ), NUMBER__from_integer_register( n ), N( colour ) );
    }
    asm ( "svc 0" );
    }

    sleep_ms( 25 );
  }
}
#endif

#define TRACING
#ifdef TRACING

static void set_block( uint32_t addr, int code )
{
  uint32_t *arm_code = (void *) ro_address.r;
  uint32_t block = addr/4;

  asm volatile ( "dc civac, %[va]" : : [va] "r" (&arm_code[block]) );
  arm_code[block] = 0xe1400070 | (code & 0xf) | ((code & 0xfff0) << 4); // hvc #code
  asm volatile ( "dc civac, %[va]" : : [va] "r" (&arm_code[block]) );
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

  // DACR
  arm_code[i++] = 0xe3a07001; // mov	r7, #1
  arm_code[i++] = 0xee037f10; // mcr	15, 0, r7, cr3, cr0, {0}

  arm_code[0x2000] = 0x00008c02; // 	Map first megabyte to byte 0

  arm_code[i++] = 0xe3a09902; // mov	r9, #32768	; 0x8000
  arm_code[i++] = 0xe3899002; // orr	r9, r9, #2
  arm_code[i++] = 0xee029f10; // mcr	15, 0, r9, cr2, cr0, {0}

  {
  arm_code[i++] = 0xe3a0947f; // mov r9, #0x78000000
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  int loop = i;
                              // loop:
  arm_code[i++] = 0xe2999001; // add r9, r9, #1
  arm_code[i++] = 0xe5819400; // str	r9, [r1, #1024]	; 0x400
  uint32_t branch = 0x5afffffe - (i - loop);
  arm_code[i++] = branch; // bpl loop
  }

  arm_code[i++] = 0xe3a0860e; // mov	r8, #14680064	; 0xe00000
  arm_code[i++] = 0xe3888c3e; // orr	r8, r8, #15872	; 0x3e00
  arm_code[i++] = 0xe388802f; // orr	r8, r8, #47	; 0x2f
  arm_code[i++] = 0xee018f10; // mcr	15, 0, r8, cr1, cr0, {0}

  {
  arm_code[i++] = 0xe3a0947f; // mov r9, #0x78000000
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  int loop = i;
                              // loop:
  arm_code[i++] = 0xe2999001; // add r9, r9, #1
  arm_code[i++] = 0xe5819400; // str	r9, [r1, #1024]	; 0x400
  uint32_t branch = 0x5afffffe - (i - loop);
  arm_code[i++] = branch; // bpl loop
  }

  arm_code[i++] = 0xee018f10; // mcr	15, 0, r8, cr1, cr0, {0}
  arm_code[i++] = 0xee018f10; // mcr	15, 0, r8, cr1, cr0, {0}
  arm_code[i++] = 0xee018f10; // mcr	15, 0, r8, cr1, cr0, {0}

  {
  arm_code[i++] = 0xe3a0947f; // mov r9, #0x78000000
  arm_code[i++] = 0xe3a01000; // mov	r1, #0
  int loop = i;
                              // loop:
  arm_code[i++] = 0xe2999001; // add r9, r9, #1
  arm_code[i++] = 0xe5819400; // str	r9, [r1, #1024]	; 0x400
  uint32_t branch = 0x5afffffe - (i - loop);
  arm_code[i++] = branch; // bpl loop
  }

  arm_code[i++] = 0xe3a0743f; // 	mov	r7, #1056964608	; 0x3f000000
  arm_code[i++] = 0xe3877602; // 	orr	r7, r7, #2097152	; 0x200000
  arm_code[i++] = 0xe587601c; // 	str	r6, [r7, #28]
  arm_code[i++] = 0xe587601c; // 	str	r6, [r7, #28]

  uint32_t branch = 0xeafffffe - i + 12;
  arm_code[i++] = branch; // b 0x30

  for (int j = 0; j < i; j++) {
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1000 ), N( j*12 + 100 ), NUMBER__from_integer_register( arm_code[j] ), N( 0xffffffff ) );
  }
  arm_code[0x100] = 0x11223344;

  static uint64_t __attribute__(( aligned( 16 ) )) stack[32];
  create_thread( qemu_timer_thread, stack + 32 );

  return true;
#else
  uint32_t crc;
  const unsigned char* rom_base = (void*) (ro_address.r + rom_load.r);

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

  return true; // The ROM is likely to change, keep going anyway
#endif
}

static struct {
  uint32_t irq_basic_pending;
  uint32_t irq_pending1;
  uint32_t irq_pending2;
  uint32_t fiq_control;
  uint32_t enabled_irqs1;
  uint32_t enabled_irqs2;
  uint32_t enabled_basic_irqs;
} volatile irq_details = { 0 };

static uint64_t irq_lock = 0;
static bool irq_active = 0;

static void update_virtual_irq()
{
  bool irq_now = ((irq_details.irq_pending1 & irq_details.enabled_irqs1) != 0)
              || ((irq_details.irq_pending2 & irq_details.enabled_irqs2) != 0)
              || ((irq_details.irq_basic_pending & irq_details.enabled_basic_irqs) != 0);

  if (irq_now && !irq_active) {
    asm ( "svc 8" );
  }
  else if (!irq_now && irq_active) {
    asm ( "svc 9" );
  }

  irq_active = irq_now;
}

static void trigger_irq( int n )
{
  claim_lock( &irq_lock );

  if (n < 32) {
    irq_details.irq_pending1 |= (1 << n);
  }
  else if (n < 64) {
    irq_details.irq_pending2 |= (1 << (n - 32));
  }
  else if (n < 72) {
    irq_details.irq_basic_pending |= (1 << (n - 64));
  }

  update_virtual_irq();

  release_lock( &irq_lock );
}

static bool irq_pending( int n )
{
  if (n < 32) {
    return 0 != (irq_details.irq_pending1 & irq_details.enabled_irqs1 & (1 << n));
  }
  else if (n < 64) {
    return 0 != (irq_details.irq_pending2 & irq_details.enabled_irqs2 & (1 << (n - 32)));
  }
  else if (n < 72) {
    return 0 != (irq_details.irq_basic_pending & irq_details.enabled_basic_irqs & (1 << (n - 64)));
  }
  return false;
}

static void clear_irq( int n )
{
  claim_lock( &irq_lock );

  if (n < 32) {
    irq_details.irq_pending1 &= ~(1 << n);
  }
  else if (n < 64) {
    irq_details.irq_pending2 &= ~(1 << (n - 32));
  }
  else if (n < 72) {
    irq_details.irq_basic_pending &= ~(1 << (n - 64));
  }

  update_virtual_irq();

  release_lock( &irq_lock );
}

static void bcm_2835_irq_registers_access( bool is_write, int register_index, uint64_t offset )
{
  claim_lock( &irq_lock );

  switch (offset) {
  case 0x200:
    if (!is_write) {
      uint32_t pending = irq_details.irq_basic_pending & irq_details.enabled_basic_irqs & 0xff;
      if (irq_pending( 62 )) pending |= (1 << 20); // EMMC/SD card interface
      if (irq_pending( 57 )) pending |= (1 << 19);
      if (irq_pending( 56 )) pending |= (1 << 18);
      if (irq_pending( 55 )) pending |= (1 << 17);
      if (irq_pending( 54 )) pending |= (1 << 16);
      if (irq_pending( 53 )) pending |= (1 << 15);
      if (irq_pending( 19 )) pending |= (1 << 14);
      if (irq_pending( 18 )) pending |= (1 << 13);
      if (irq_pending( 10 )) pending |= (1 << 12);
      if (irq_pending( 9 )) pending |= (1 << 11);
      if (irq_pending( 7 )) pending |= (1 << 10);
      if (irq_details.irq_pending1 & irq_details.enabled_irqs1 != 0) pending |= (1 << 9);
      if (irq_details.irq_pending2 & irq_details.enabled_irqs2 != 0) pending |= (1 << 8);
      set_partner_register( register_index, pending );
    }
    break;

  case 0x204:
    if (!is_write) {
      set_partner_register( register_index, irq_details.irq_pending1 & irq_details.enabled_irqs1 );
    }
    break;

  case 0x208:
    if (!is_write) {
      set_partner_register( register_index, irq_details.irq_pending2 & irq_details.enabled_irqs2 );
    }
    break;

  case 0x20c:
    if (is_write) {
      irq_details.fiq_control = get_partner_register( register_index );
      asm ( "brk 1" );
    }
    else {
      set_partner_register( register_index, irq_details.fiq_control );
    }
    break;

  case 0x210:
    if (is_write) {
      irq_details.enabled_irqs1 |= get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, irq_details.enabled_irqs1 );
    }
    break;

  case 0x214:
    if (is_write) {
      irq_details.enabled_irqs2 |= get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, irq_details.enabled_irqs2 );
    }
    break;

  case 0x218:
    if (is_write) {
      irq_details.enabled_basic_irqs |= get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, irq_details.enabled_basic_irqs );
    }
    break;

  case 0x21c:
    if (is_write) {
      irq_details.enabled_irqs1 &= ~get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, irq_details.enabled_irqs1 );
    }
    break;

  case 0x220:
    if (is_write) {
      irq_details.enabled_irqs2 &= ~get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, irq_details.enabled_irqs2 );
    }
    break;

  case 0x224:
    if (is_write) {
      irq_details.enabled_basic_irqs &= ~get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, irq_details.enabled_basic_irqs );
    }
    break;

  default: asm ( "brk 1" ); // bcm_2835_irq_registers_access
  }

  if (is_write) {
    update_virtual_irq();
  }

  release_lock( &irq_lock );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1250 ), N( 50 ), N( irq_details.irq_basic_pending ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1250 ), N( 60 ), N( irq_details.irq_pending1 ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1250 ), N( 70 ), N( irq_details.irq_pending2 ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1250 ), N( 80 ), N( irq_details.fiq_control ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1250 ), N( 90 ), N( irq_details.enabled_irqs1 ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1250 ), N( 100 ), N( irq_details.enabled_irqs2 ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1250 ), N( 110 ), N( irq_details.enabled_basic_irqs ), N( 0xff00ff00 ) );
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
#define WRITE_ONLY if (cp.is_read) asm ( "brk 1" );
#define READ_WRITE( n ) static uint64_t v = n; if (cp.is_read) set_partner_register( cp.Rt, v ); else v = get_partner_register( cp.Rt );

static int y = 40;
static int x = 100;
static int elc = 0xff0ff0f0;
static bool show;


static void cp15_access( uint32_t syndrome )
{
  copro_syndrome cp = { .raw = syndrome };
/*
if ((syndrome & 0xffc1e) != 0x41c14
 && (syndrome & 0xffc1e) != 0x21c14
 && (syndrome & 0xffc1e) != 0x81c14
 && (syndrome & 0xffc1e) != 0x81c0a
) {
*/ if (show) {
y+=10;
if (y > 1000) {
  y = 50;
  x += 100;
  if (x > 800) {
    x = 100;
    elc = elc ^ 0x00ffffff;
  }
}
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x ), N( y ), N( cp.Opc1 ), N( elc ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+10 ), N( y ), N( cp.CRn ), N( elc ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+20 ), N( y ), N( cp.CRm ), N( elc ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+30 ), N( y ), N( cp.Opc2 ), N( elc ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+40 ), N( y ), N( cp.is_read ), N( elc ) );
TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( x+50 ), N( y ), N( cp.Rt ), N( elc ) );
asm ( "svc 0" );
}

  switch (syndrome & 0xffc1e) { // CRm, CRn, Opc1, Opc2
  case 0x00000: READ_ONLY( 0x410fb767 ); break; // midr ~ ARM1176 - does VPIDR_EL2 ever get used?  ARM11_HAL
  case 0xa0000: READ_ONLY( 0x80000f00 ); break; // mpidr
  case 0x20000: READ_ONLY( 0x1d152152 ); break; // G7.2.34    CTR, Cache Type Register
  case 0x00400: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( SCTLR_EL1, v ); } }; break; // SCTLR
  case 0x00c00: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( DACR32_EL2, v ); } } break; // G7.2.35 DACR, Domain Access Control Register
  case 0x40800: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( TCR_EL1, v ); } }; break; // Translation Table Base Control Register
  case 0x00800: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( TTBR0_EL1, v ); ttbr0_el1 = v; } }; break; // Translation Table Base Register 0
  //case 0x20400: { READ_WRITE( 7 ); if (!cp.is_read) { set_vm_system_register( ACTLR_EL1, v ); } }; break; // G7.2.1 ACTLR, Auxiliary Control Register  -- Fixing errata in Kernel/s/HAL:1031
  case 0x20400: { READ_WRITE( 7 ); }; break; // G7.2.1 ACTLR, Auxiliary Control Register

  // Question: why are these being trapped, I thought that was optional...?
  // Currently, the extra code in EL2 should perform these functions, but for every event, which is not good.
  case 0x01c0a: WRITE_ONLY; break; // G7.2.76    ICIALLU, Instruction Cache Invalidate All to PoU
  case 0x0200e: WRITE_ONLY; break; // G7.2.130   TLBIALL, TLB Invalidate All
  case 0xc1c0a: WRITE_ONLY; break; // G7.2.21    BPIALL, Branch Predictor Invalidate All
  case 0x2200a: WRITE_ONLY; break; // Invalidate Instruction TLB Entry by MVA ARM DDI 0333H 3-87
  case 0x2200c: WRITE_ONLY; break; // Invalidate Data TLB Entry by MVA ARM DDI 0333H 3-87

// Can I do these two?
  // Note: RISC OS doesn't seem to use the equivalent registers for instruction aborts, lr is enough.
  case 0x01800: // G7.2.43 DFAR, Data Fault Address Register
    {
      if (cp.is_read) {
        set_partner_register( cp.Rt, get_vm_system_register( FAR_EL1 ) );
      }
      else {
        set_vm_system_register( FAR_EL1, get_partner_register( cp.Rt ) );
      }
      break;
    }
  case 0x01400: // G7.2.?? DFSR, Data Fault Status Register
    {
      if (cp.is_read) {
        set_partner_register( cp.Rt, get_vm_system_register( ESR_EL1 ) );
      }
      else {
        set_vm_system_register( ESR_EL1, get_partner_register( cp.Rt ) );
      }
      break;
    }

  // FIXME Can't really need to do a full flush, surely?
  case 0x81c14: WRITE_ONLY; asm ( "dsb sy" ); asm ( "svc 0" ); break; // G7.2.29    CP15DSB, Data Synchronization Barrier System instruction
  case 0x81c0a: WRITE_ONLY; asm ( "isb sy" ); asm ( "svc 0" ); break; // G7.2.30    CP15ISB, Instruction Synchronization Barrier System instruction
  case 0x24000: WRITE_ONLY; break; // G7.2.26    CLIDR, Cache Level ID Register
  case 0x01c0e: WRITE_ONLY; asm ( "isb sy\ndsb sy" ); break; // ARM DDI 0360F Invalidate Both Caches. Also flushes the branch target cache
  case 0xa1c14: WRITE_ONLY; asm ( "dmb sy" ); break; // G7.2.28         CP15DMB, Data Memory Barrier System instruction
  case 0x01c1c: WRITE_ONLY; asm ( "dmb sy" ); break; // Data Memory Barrier

  case 0x41c14: WRITE_ONLY; break; // G7.2.40 DCCSW, Data Cache line Clean by Set/Way
  case 0x21c14: break; // G7.2.38 DCCMVAC, Data Cache line Clean by VA to PoC


  case 0x00002: READ_ONLY( 0x00000111 ); break; // Read Proc Feature Register 0 (typo in ARM DDI 0360F)
  case 0x20002: READ_ONLY( 0x00000001 ); break; // Read Proc Feature Register 1
  case 0x40002: READ_ONLY( 0x00000002 ); break; // Read Debug Feature Register 0
  case 0x80002: READ_ONLY( 0x01100103 ); break; // Read Memory Model Feature Register 0
  case 0xa0002: READ_ONLY( 0x10020302 ); break; // Read Memory Model Feature Register 1
  case 0xc0002: READ_ONLY( 0x01222000 ); break; // Read Memory Model Feature Register 2
  case 0xe0002: READ_ONLY( 0x00000000 ); break; // Read Memory Model Feature Register 3

  // The following values match the ARM11, but may not be supported by the 64-bit machine...
  case 0x00004: READ_ONLY( 0x00100011 ); break; // ID_ISAR0, Instruction Set Attribute Register 0
  case 0x20004: READ_ONLY( 0x12002111 ); break; // ID_ISAR1, Instruction Set Attribute Register 1
  case 0x40004: READ_ONLY( 0x11221011 ); break; // ID_ISAR2, Instruction Set Attribute Register 2
  case 0x60004: READ_ONLY( 0x01102131 ); break; // ID_ISAR3, Instruction Set Attribute Register 3
  case 0x80004: READ_ONLY( 0x00000141 ); break; // ID_ISAR4, Instruction Set Attribute Register 4
  case 0xa0004: READ_ONLY( 0x00000000 ); break; // ID_ISAR5, Instruction Set Attribute Register 5 - not in ARM11? - not in ARM11?
  default: asm ( "brk 65" );
  }
asm ( "svc 0" );
}

static void cp14_access( uint32_t syndrome )
{
  copro_syndrome cp = { .raw = syndrome };

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

/* This implements the HAL for RISC OS. */

enum HAL { 
HAL_IRQEnable = 0x1,
HAL_IRQDisable = 0x2,
HAL_IRQClear = 0x3,
HAL_IRQSource = 0x4,
HAL_IRQStatus = 0x5,
HAL_FIQEnable = 0x6,
HAL_FIQDisable = 0x7,
HAL_FIQDisableAll = 0x8,
HAL_FIQClear = 0x9,
HAL_FIQSource = 0xA,
HAL_FIQStatus = 0xB,
HAL_Timers = 0xC,
HAL_TimerDevice = 0xD,
HAL_TimerGranularity = 0xE,
HAL_TimerMaxPeriod = 0xF,
HAL_TimerSetPeriod = 0x10,
HAL_TimerPeriod = 0x11,
HAL_TimerReadCountdown = 0x12,
HAL_CounterRate = 0x13,
HAL_CounterPeriod = 0x14,
HAL_CounterRead = 0x15,
HAL_CounterDelay = 0x16 };

uint32_t timer_period = 0;

static void hvc32( uint32_t syndrome ) { 

  // Isambard-aware RISC OS

  switch ((syndrome & 0xffff)) {
  case 0: // Init, nothing to do? It sets up a bunch of stuff...
    asm ( "brk 77" ); // Shouldn't get called, trapped in OS
    break;

  case HAL_IRQEnable: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_IRQDisable: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_IRQClear: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_IRQSource: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_IRQStatus: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_FIQEnable: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_FIQDisable: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_FIQDisableAll: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_FIQClear: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_FIQSource: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_FIQStatus: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_Timers: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_TimerDevice: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_TimerGranularity:
    {
    switch (get_partner_register( 0 )) {
    case 0: set_partner_register( 0, 100 ); break;
    default: asm( "brk 0x9000" );
    }
    }
    break;
  case HAL_TimerMaxPeriod: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_TimerSetPeriod:
    timer_period = get_partner_register( 0 );
    break;
  case HAL_TimerPeriod:
    set_partner_register( 0, timer_period );
    break;
  case HAL_TimerReadCountdown: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_CounterRate: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    set_partner_register( 0, 1000 );
    break;
  case HAL_CounterPeriod: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    set_partner_register( 0, timer_period ); break;
    }
    break;
  case HAL_CounterRead: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    }
    break;
  case HAL_CounterDelay: asm( "brk %[n]" : : [n] "i" (__LINE__) );
    {
    sleep_ms( get_partner_register( 0 ) / 1000 );
    }
    break;

  case 86: // DebugTX, nothing?
{
static int x = 10;
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 10 ), N( 1060 ), N( get_partner_register( 4 ) ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( x ), N( 1070 ), N( get_partner_register( 0 ) ), N( 0xffff00ff ) );
x += 20;
}
    break;
  case 87: // DebugRX, from where?
    break;
  case 107: // 0xb6 - HAL_IRQMax
    set_partner_register( 0, 64 + 21 );
    break;
  case 0x5555: // Crashed?
 asm( "mov x0, %[p]\nsvc 10" : : [p] "r" (vm_memory_base + 0x0200b000) : "x0" );
  default:asm ( "brk 3" );
  }
}

static void smc32( uint32_t syndrome ) { asm ( "brk 1" ); }

static void respond_to_tag_request( uint32_t *request )
{
  uint32_t *p = request;

  // Undocumented interface, afaics
  uint32_t *virtual_Touch_buffer = 0;
  uint32_t *virtual_GPIO_buffer = 0;

  // Always assumes correct use
  p[1] = 0x80000000;
  p+=2;
  while (p[0] != 0) {
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 440 ), N( p[0] ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 450 ), N( p[1] ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 460 ), N( p[2] ), N( 0xffff00ff ) );
    // Always a valid request and response...
    // p[0] = tag
    // p[1] = buffer size (bytes)
    // p[2] = request code 0, response code 0x80000000 | length
    // p[3] = first word
    p[2] = p[1] | (1<<31);
    switch (p[0]) {
    case 0x00010001: // Board model
      p[3] = 0; // RISC OS locks up in HAL_Init if this is not zero!
      break;
    case 0x00010002: // Board revision
      p[3] = 13; // Model B, 512MB https://elinux.org/RPi_HardwareHistory#Board_Revision_History NOT NewScheme!
      break;
    case 0x00010003: // MAC address
      p[2] = 6 | (1<<31);
      p[3] = 0x2a2a2a2a; // Nobody cares, afaics!
      p[4] = 0x2a2a;
      break;
    case 0x00010004: // Get board serial
      p[3] = 0x2a2a2a2a; // Nobody cares, afaics!
      p[4] = 0x2a2a2a2a;
      break;
    case 0x00010005: // Get ARM memory
      p[3] = 0;
      p[4] = riscos_ram_size.r;
      break;
    case 0x00010006: // Get VC memory
      p[3] = 0;
      p[4] = 0;
      break;
    case 0x00060001: // Get DMA channels
      p[3] = (1 << 4);
      break;
    case 0x00040002: // Blank screen (not RISC OS controlled)
      break;
    case 0x00030002: // Get clock rate
      switch (p[3]) {
      case 1: p[4] = 250000; break; // EMMC
      case 4: p[4] = 250000000; break; // CORE
      default:
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 440 ), N( p[0] ), N( 0xff0ff0f0 ) );
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 450 ), N( p[3] ), N( 0xff0ff0f0 ) );
        asm ( "brk 1" );
      }
      break;
    case 0x00038002: // Set clock rate
      switch (p[3]) {
      case 2: break; // UART
      default:
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 440 ), N( p[0] ), N( 0xff0ff0f0 ) );
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 450 ), N( p[3] ), N( 0xff0ff0f0 ) );
        asm ( "brk 1" );
      }
      break;

    case 0x0004801f: // ARM2VC_Tag_FBSetTouchBuf Touch screen
      virtual_Touch_buffer = p[3];
      // RISC OS: ; On success, firmware should overwrite the address with zero
      p[3] = 0;
      break;

    case 0x00048020: // ARM2VC_Tag_SetVirtGPIOBuf I don't know what this is for!
      virtual_GPIO_buffer = p[3];
      // RISC OS: ; On success, firmware should overwrite the address with zero
      p[3] = 0;
      break;

    // Frame buffer

    default: // Unknown
      asm ( "brk 1" );
      break;
    }
    p += 3 + p[1]/4;
  }
}

static struct {
  uint32_t control_status;
  uint64_t counter;
  uint32_t compare[4];
} volatile system_timer = { 0 };

static bool timer_passed( uint32_t now, uint32_t ticks, uint32_t match )
{
  bool wraps = (now + ticks) < now;
  if (wraps) return (match > now) || (match < (now + ticks));
  return (match > now) && (match < (now + ticks));
}

static void timer_thread()
{
  uint32_t *arm_code = (void *) ro_address.r;
  uint32_t count = 0;
  for (;;) {
    uint32_t colour = (system_timer.control_status != 0) ? 0xff00ff00 : 0xffffffff;
    asm ( "svc 0" );
    if ((count & 0x3ff) == 0) {
uint32_t pc = 0;
      TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 570 ), NUMBER__from_integer_register( count ), N( 0xffffffff ) );
    for (int i = 0; i < 35; i++) {
      uint64_t n;
      asm ( "mov x0, %[n]\nsvc 7\nmov %[r], x0" : [r] "=&r" (n) : [n] "r" (i) : "x0" );
if (i == 32) pc = n;
      TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 600 + 10*i ), NUMBER__from_integer_register( n ), N( colour ) );
    }
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 40 ), N( system_timer.control_status ), N( colour ) );
TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1500 ), N( 50 ), N( system_timer.counter ), N( colour ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 60 ), N( system_timer.compare[0] ), N( system_timer.control_status & 1 ? 0xffff0000 : 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 70 ), N( system_timer.compare[1] ), N( system_timer.control_status & 2 ? 0xffff0000 : 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 80 ), N( system_timer.compare[2] ), N( system_timer.control_status & 4 ? 0xffff0000 : 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 90 ), N( system_timer.compare[3] ), N( system_timer.control_status & 8 ? 0xffff0000 : 0xff00ff00 ) );
static stop_count = 0;
if (++stop_count == 100000 || pc == 0xffff000c) {
    stop_count = 0;
    show_state();
    asm ( "smc 7" );
    uint64_t r;
    asm ( "mov x0, %[n]\nsvc 7\nmov %[r], x0" : [r] "=&r" (r) : [n] "r" (32) : "x0" );
}
    }

    // This is an inaccurate clock, it's probably worth giving RO a proper timer, triggered by interrupt.
    const uint32_t ticks = 1000;
    uint32_t now = system_timer.counter;
    system_timer.counter += ticks;
    for (int i = 0; i < 4; i++) {
      if (timer_passed( now, ticks, system_timer.compare[i] )) {
        system_timer.control_status |= (1 << i);
        trigger_irq( i ); // Only irq 1 is used by RISC OS, the others are an educated guess.
      }
    }

    sleep_ms( 1 );
  }
}

static void bcm_2835_system_timer_access( bool is_write, int register_index, uint64_t offset )
{
  // FIXME! First access at 0fc009674
  static uint32_t locked_high = 0;

  switch (offset) {
  case 0x00:
    if (is_write) {
      system_timer.control_status &= ~get_partner_register( register_index );
      for (;;) {}
    }
    else {
      set_partner_register( register_index, system_timer.control_status );
    }
    break;
  case 0x04: 
    {
    uint64_t ticks = system_timer.counter;
    locked_high = (ticks >> 32);

    set_partner_register( register_index, ticks & 0xffffffff );
    }
    break;
  case 0x08: 
    set_partner_register( register_index, locked_high );
    break;
  case 0x0c ... 0x18: 
  {
    int n = (offset >> 4) & 3;
    if (is_write) {
      system_timer.compare[n] = get_partner_register( register_index );
if (system_timer.compare[n] < system_timer.counter & 0xffffffff) {
// FIXME!
system_timer.compare[n] = system_timer.counter + 2500;
}
    }
    else {
      set_partner_register( register_index, system_timer.compare[n] );
      // TODO: set a system_timer to interrupt the VM...
    }
    break;
  }
  default: asm ( "brk 1" );
  }

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1350 ), N( 40 ), N( system_timer.control_status ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1350 ), N( 50 ), N( system_timer.counter ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1350 ), N( 60 ), N( system_timer.compare[0] ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1350 ), N( 70 ), N( system_timer.compare[1] ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1350 ), N( 80 ), N( system_timer.compare[2] ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1350 ), N( 90 ), N( system_timer.compare[3] ), N( 0xff00ff00 ) );
}

extern struct __attribute__(( packed )) {
  uint32_t bsc0[1024];
} volatile  __attribute__(( aligned(4096) )) devices;

static void bsc_access( unsigned device, bool is_write, int register_index, uint64_t offset )
{
  if (device >= 3) asm ( "brk 1" );

  if (device == 1) {
NUMBER colours[2] = { { .r = 0xff00ff00 }, { .r = 0xff00ffff } };
static unsigned y = 600;

    if (is_write) {
      devices.bsc0[offset/4] = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, devices.bsc0[offset/4] );
    }
TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( 1160 ), N( y ), N( offset ), colours[is_write] );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1200 ), N( y ), N( get_partner_register( register_index ) ), colours[is_write] );
y += 10;
if (y >= 1000) y -= 398;
    return;
  }
  else {
    asm ( "brk 1" );
  }
  static struct {
    uint32_t Control;
    uint32_t Status;
    uint32_t Data_Length;
    uint32_t Slave_Address;
    uint32_t Data_FIFO;
    uint32_t Clock_Divider;
    uint32_t Data_Delay;
    uint32_t Clock_Stretch_Timeout;
  } volatile BSC[3] = { // BCM2835-ARM-Peripherals.pdf Section 3 (IIC/I2C).
    { .Status = 0x50, .Clock_Divider = 0x5dc, .Data_Delay = 0x00300030, .Clock_Stretch_Timeout = 0x40 },
    { .Status = 0x50, .Clock_Divider = 0x5dc, .Data_Delay = 0x00300030, .Clock_Stretch_Timeout = 0x40 },
    { .Status = 0x50, .Clock_Divider = 0x5dc, .Data_Delay = 0x00300030, .Clock_Stretch_Timeout = 0x40 }
  };

  uint8_t *data = 0;

// FIXME Pretend to be something on the other end of the bus (or allow access to the real hardware)
static unsigned y = 600;
if (is_write) {
TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( 1160 ), N( y ), N( offset ), N( 0xff00ff00 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1200 ), N( y ), N( get_partner_register( register_index ) ), N( 0xff00ff00 ) );
}
else {
TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( 1160 ), N( y ), N( offset ), N( 0xff00ffff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1200 ), N( y ), N( (&BSC[device].Control)[offset/4] ), N( 0xff00ffff ) );
}
y += 10;
if (y >= 1000) y -= 398;

  if (device >= 3) asm ( "brk 1" );

  switch (offset) {
  case 0x00: // Control
    {
      if (is_write) {
        BSC[device].Control = get_partner_register( register_index );
        if (BSC[device].Control == 0x8011) {
          // Clear FIFO and read from set address
          switch (BSC[device].Slave_Address) {
          case 0x68: // ???
            data = "Hello world";
            break;
          default: asm ( "brk 1" );
          }
        }
      }
      else {
        set_partner_register( register_index, BSC[device].Control );
      }
    }
    break;
  case 0x04: // Status
    {
      if (is_write) {
        BSC[device].Status &= ~get_partner_register( register_index );
      }
      else {
        set_partner_register( register_index, BSC[device].Status | ((data != 0) ? 0b100001 : 0) );
      }
    }
    break;
  case 0x08: // Data_Length
    {
      if (is_write) {
        BSC[device].Data_Length = get_partner_register( register_index );
      }
      else {
        set_partner_register( register_index, BSC[device].Data_Length );
      }
    }
    break;
  case 0x0c: // Slave_Address
    {
      if (is_write) {
        BSC[device].Slave_Address = get_partner_register( register_index );
      }
      else {
        set_partner_register( register_index, BSC[device].Slave_Address );
      }
    }
    break;
  case 0x10: // Data_FIFO
    {
      if (is_write) {
        asm ( "brk 1" );
      }
      else {
        set_partner_register( register_index, data == 0 ? 0 : *data++ );
      }
    }
    break;
  case 0x14: // Clock_Divider
    {
      if (is_write) {
        BSC[device].Clock_Divider = get_partner_register( register_index );
      }
      else {
        set_partner_register( register_index, BSC[device].Clock_Divider );
      }
    }
    break;
  case 0x18: // Data_Delay
    {
      if (is_write) {
        BSC[device].Data_Delay = get_partner_register( register_index );
      }
      else {
        set_partner_register( register_index, BSC[device].Data_Delay );
      }
    }
    break;
  case 0x1c: // Clock_Stretch_Timeout
    {
      if (is_write) {
        BSC[device].Clock_Stretch_Timeout = get_partner_register( register_index );
      }
      else {
        set_partner_register( register_index, BSC[device].Clock_Stretch_Timeout );
      }
    }
    break;
  default: asm ( "brk 1" );
  }
}

static struct {
  uint32_t ARG2; // 0x0 ACMD23 Argument
  uint32_t BLKSIZECNT; // 0x4 Block Size and Count
  uint32_t ARG1; // 0x8 Argument
  uint32_t CMDTM; // 0xc Command and Transfer Mode
  uint32_t RESP0; // 0x10 Response bits 31 : 0
  uint32_t RESP1; // 0x14 Response bits 63 : 32
  uint32_t RESP2; // 0x18 Page Response bits 95 : 64
  uint32_t RESP3; // 0x1c Response bits 127 : 96
  uint32_t DATA; // 0x20 Data
  uint32_t STATUS; // 0x24 Status
  uint32_t CONTROL0; // 0x28 Host Configuration bits
  uint32_t CONTROL1; // 0x2c Host Configuration bits
  uint32_t INTERRUPT; // 0x30 Interrupt Flags
  uint32_t IRPT_MASK; // 0x34 Interrupt Flag Enable
  uint32_t IRPT_EN; // 0x38 Interrupt Generation Enable
  uint32_t CONTROL2; // 0x3c Host Configuration bits
  uint32_t FORCE_IRPT; // 0x50 Force Interrupt Event
  uint32_t BOOT_TIMEOUT; // 0x70 Timeout in boot mode
  uint32_t DBG_SEL; // 0x74 Debug Bus Configuration
  uint32_t EXRDFIFO_CFG; // 0x80 Extension FIFO Configuration
  uint32_t EXRDFIFO_EN; // 0x84 Extension FIFO Enable
  uint32_t TUNE_STEP; // 0x88 Delay per card clock tuning step
  uint32_t TUNE_STEPS_STD; // 0x8c Card clock tuning steps for SDR
  uint32_t TUNE_STEPS_DDR; // 0x90 Card clock tuning steps for DDR
  uint32_t SPI_INT_SPT; // 0xf0 SPI Interrupt Support
  uint32_t SLOTISR_VER; // 0xfc Slot Interrupt Status and Version
} volatile emmc_state = { .CONTROL1 = 7 };

// Use for all values that can be affected by more than one thread
static uint64_t emmc_state_lock = 0;

static uint32_t sdcard_thread = 0;

static void sdcard()
{
  sdcard_thread = this_thread;

  for (;;) {
    wait_until_woken();
    switch (emmc_state.CMDTM >> 24) {
    case 0x07: // CMD7_SELECT_DESELECT_CARD
      sleep_ms( 1 ); // Timing check?
      claim_lock( &emmc_state_lock );
      emmc_state.STATUS &= ~1; // CMD_INHIBIT
      emmc_state.INTERRUPT |= 1; // CMD_DONE
      if (0 != (emmc_state.INTERRUPT & emmc_state.IRPT_MASK)) {
        trigger_irq( 62 ); // Also basic pending bit 20
      }
      release_lock( &emmc_state_lock );
      break;
    default: asm ( "brk 1" );
    }
  }
}

static void bcm_2835_emmc_access( bool is_write, int register_index, uint64_t offset )
{
  claim_lock( &emmc_state_lock );

  switch (offset) {
  case 0x0: // ARG2
    if (is_write) {
      emmc_state.ARG2 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.ARG2 );
    }
    break;
  case 0x4: // BLKSIZECNT
    if (is_write) {
      emmc_state.BLKSIZECNT = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.BLKSIZECNT );
    }
    break;
  case 0x8: // ARG1
    if (is_write) {
      emmc_state.ARG1 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.ARG1 );
    }
    break;
  case 0xc: // CMDTM
    if (is_write) {
      emmc_state.CMDTM = get_partner_register( register_index );
      emmc_state.STATUS |= 1; // CMD_INHIBIT
      wake_thread( sdcard_thread );
      yield(); // Needed? Not good. FIXME
    }
    else {
      set_partner_register( register_index, emmc_state.CMDTM );
    }
    break;
  case 0x10: // RESP0
    if (is_write) {
      emmc_state.RESP0 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.RESP0 );
    }
    break;
  case 0x14: // RESP1
    if (is_write) {
      emmc_state.RESP1 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.RESP1 );
    }
    break;
  case 0x18: // RESP2
    if (is_write) {
      emmc_state.RESP2 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.RESP2 );
    }
    break;
  case 0x1c: // RESP3
    if (is_write) {
      emmc_state.RESP3 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.RESP3 );
    }
    break;
  case 0x20: // DATA
    if (is_write) {
      emmc_state.DATA = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.DATA );
    }
    break;
  case 0x24: // STATUS
    if (is_write) {
      emmc_state.STATUS = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.STATUS );
    }
    break;
  case 0x28: // CONTROL0
    if (is_write) {
      emmc_state.CONTROL0 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.CONTROL0 );
    }
    break;
  case 0x2c: // CONTROL1
    if (is_write) {
      emmc_state.CONTROL1 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.CONTROL1 );
    }
    break;
  case 0x30: // INTERRUPT
    if (is_write) {
      emmc_state.INTERRUPT &= ~get_partner_register( register_index );
      if (0 == (emmc_state.INTERRUPT & emmc_state.IRPT_MASK)) {
        clear_irq( 62 ); // Also basic pending bit 20
      }
    }
    else {
      set_partner_register( register_index, emmc_state.INTERRUPT );
    }
    break;
  case 0x34: // IRPT_MASK
    if (is_write) {
      emmc_state.IRPT_MASK = get_partner_register( register_index );
      if (0 != (emmc_state.INTERRUPT & emmc_state.IRPT_MASK)) {
        trigger_irq( 62 ); // Also basic pending bit 20
      }
    }
    else {
      set_partner_register( register_index, emmc_state.IRPT_MASK );
    }
    break;
  case 0x38: // IRPT_EN
    if (is_write) {
      emmc_state.IRPT_EN = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.IRPT_EN );
    }
    break;
  case 0x3c: // CONTROL2
    if (is_write) {
      emmc_state.CONTROL2 = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.CONTROL2 );
    }
    break;
  case 0x50: // FORCE_IRPT
    if (is_write) {
      emmc_state.FORCE_IRPT = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.FORCE_IRPT );
    }
    break;
  case 0x70: // BOOT_TIMEOUT
    if (is_write) {
      emmc_state.BOOT_TIMEOUT = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.BOOT_TIMEOUT );
    }
    break;
  case 0x74: // DBG_SEL
    if (is_write) {
      emmc_state.DBG_SEL = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.DBG_SEL );
    }
    break;
  case 0x80: // EXRDFIFO_CFG
    if (is_write) {
      emmc_state.EXRDFIFO_CFG = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.EXRDFIFO_CFG );
    }
    break;
  case 0x84: // EXRDFIFO_EN
    if (is_write) {
      emmc_state.EXRDFIFO_EN = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.EXRDFIFO_EN );
    }
    break;
  case 0x88: // TUNE_STEP
    if (is_write) {
      emmc_state.TUNE_STEP = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.TUNE_STEP );
    }
    break;
  case 0x8c: // TUNE_STEPS_STD
    if (is_write) {
      emmc_state.TUNE_STEPS_STD = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.TUNE_STEPS_STD );
    }
    break;
  case 0x90: // TUNE_STEPS_DDR
    if (is_write) {
      emmc_state.TUNE_STEPS_DDR = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.TUNE_STEPS_DDR );
    }
    break;
  case 0xf0: // SPI_INT_SPT
    if (is_write) {
      emmc_state.SPI_INT_SPT = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.SPI_INT_SPT );
    }
    break;
  case 0xfc: // SLOTISR_VER
    if (is_write) {
      emmc_state.SLOTISR_VER = get_partner_register( register_index );
    }
    else {
      set_partner_register( register_index, emmc_state.SLOTISR_VER );
    }
    break;
  default: asm ( "brk 1" );
  }

  release_lock( &emmc_state_lock );
}

static void stage2_data_abort( uint64_t syndrome, uint64_t fault_address, uint64_t intermediate_physical_address )
{
  union {
    uint32_t raw;
    struct {
      uint32_t DFSC:6;  // Data Fault Status Code.
      uint32_t WnR:1;   // Write/not Read
      uint32_t S1PTW:1; // S2 access fault during S1 TT walk
      uint32_t CM:1;    // Cache maintenance?
      uint32_t EA:1;    // External abort?
      uint32_t FnV:1;   // FAR NOT valid
      uint32_t SET:2;   // Synchronous Error Type
      uint32_t res0:1;
      uint32_t AR:1;    // Acquire-release
      uint32_t SF:1;    // 64-bit register
      uint32_t SRT:5;   // Register Transfer
      uint32_t SSE:1;   // Sign Extend.
      uint32_t SAS:2;   // Access Size.
      uint32_t ISV:1;   // Above holds valid data?
    };
  } s = { .raw = syndrome };

  if (!s.ISV) asm ( "brk 1" );

  static uint32_t mailbox_request = 0xffffffff;
#define BASE 0x20000000
//#define BASE 0x3f000000
#define PAGE( low ) BASE | low ... BASE | low | 0xfff

  uint64_t physical_fault_address = (intermediate_physical_address << 8) | (fault_address & 0xfff);

TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1500 ), N( 380 ), NUMBER__from_integer_register( physical_fault_address ), N( 0xffff0000 ) );

  switch (physical_fault_address) {
  case BASE | 0x100020: // Power management, reset status
    {
      if (!s.WnR) {
        set_partner_register( s.SRT, 1 << 12 ); // Report power on reset
      }
      else { asm ( "brk 1" ); }
      break;
    }

  case BASE | 0x00b200 ... BASE | 0x00b2ff:
    bcm_2835_irq_registers_access( s.WnR, s.SRT, physical_fault_address & 0xfff ); break;

  case BASE | 0x00b880: // Mailbox 0 data, return the last request that we processed when it was received
    {
      if (!s.WnR) {
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 420 ), NUMBER__from_integer_register( mailbox_request ), N( 0xff00ff00 ) );
        set_partner_register( s.SRT, mailbox_request );
      }
      else { asm ( "brk 1" ); }
      break;
    }
  case BASE | 0x00b898: // Mailbox 0 status (always ready)
    {
      if (!s.WnR) {
        set_partner_register( s.SRT, 0 );
      }
      else { asm ( "brk 1" ); }
      break;
    }
  case BASE | 0x00b8a0: // Mailbox 1 data
    {
      mailbox_request = get_partner_register( s.SRT );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 400 ), NUMBER__from_integer_register( mailbox_request ), N( 0xffff0000 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 410 ), NUMBER__from_integer_register( s.SRT ), N( 0xffff0000 ) );

      if (s.WnR) {
        mailbox_request = get_partner_register( s.SRT );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 420 ), NUMBER__from_integer_register( mailbox_request ), N( 0xffff0000 ) );
asm ( "svc 0" );

        if ((mailbox_request & 0xf) == 8) {
          uint32_t *arm_code = (void *) ro_address.r;
          respond_to_tag_request( &arm_code[(0x3ffffff0 & mailbox_request)/4] );
        }
        else if ((mailbox_request & 0xf) == 0) {
          // Power control, not RISC OS' problem
        }
        else { asm ( "brk 1" ); }
      }
      else { asm ( "brk 1" ); }
      break;
    }

  //case BASE | 0x200000 ... BASE | 0x20fff: // GPIO
  case BASE | 0x200000: // GPIO
    {
    }
    break;

  case PAGE( 0x205000 ): // BSC0 control
    bsc_access( 0, s.WnR, s.SRT, physical_fault_address & 0xfff ); break;

  case PAGE( 0x804000 ): // BSC1 control
    bsc_access( 1, s.WnR, s.SRT, physical_fault_address & 0xfff ); break;

  case PAGE( 0x805000 ): // BSC2 control
    bsc_access( 2, s.WnR, s.SRT, physical_fault_address & 0xfff ); break;

  case PAGE( 0x003000 ):
    bcm_2835_system_timer_access( s.WnR, s.SRT, physical_fault_address & 0xfff ); break;

  case PAGE( 0x300000 ):
    bcm_2835_emmc_access( s.WnR, s.SRT, physical_fault_address & 0xfff ); break;

  default:
    asm ( "brk 1" );
  }
}

uint64_t vm_handler( uint64_t pc, uint64_t syndrome, uint64_t fault_address, uint64_t intermediate_physical_address )
{
  static int count = 0;
  uint64_t new_pc = pc; // Retry instruction by default

if ((syndrome & 0xfc000000) == 0x48000000) {
static int y = 30;
TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( 1100 ), N( y ), N( syndrome & 0xff ), N( 0xffffffff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1120 ), N( y ), N( pc ), N( 0xffffffff ) );
y += 10;
if (y > 1000) y = 30;
}

static uint32_t recent[5] = { 0 };
show = true;
for (int i = 0; show && i < 5; i++) {
  if (recent[i] == fault_address) {
    //show = false;
  }
}
if (show) {
  recent[0] = recent[1];
  recent[1] = recent[2];
  recent[2] = recent[3];
  recent[3] = recent[4];
  recent[4] = fault_address;
}

if (show) {
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 200 ), N( syndrome ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 210 ), N( fault_address ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 220 ), N( intermediate_physical_address ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 230 ), N( pc ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 240 ), N( count++ ), N( 0xfffff0f0 ) );

  for (int i = 0; i < 31; i++) {
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 260+11*i ), N( get_partner_register( i ) ), N( 0xffff8080 ) );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( x-70 ), N( y ), N( pc ), N( elc ) );
  }

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( x-70 ), N( y+10 ), N( pc ), N( 0xffffffff ) );
}

  switch (syndrome >> 26) {
  case 0b000000: asm ( "brk 0b000000" ); break; // Never happens or is unimplemented
  case 0b000001: wait_until_woken(); break; // WFI or WFE
  case 0b000010: asm ( "brk 0b000010" ); break; // Never happens or is unimplemented
  case 0b000011: cp15_access( syndrome ); new_pc += 4; break;
  case 0b000100: cp15_access_RR( syndrome ); new_pc += 4; break;
  case 0b000101: cp14_access( syndrome ); new_pc += 4; break;
  case 0b000110: ldstc( syndrome ); new_pc += 4; break;
  case 0b000111: fpaccess( syndrome ); new_pc += 4; break;
  case 0b001000: vmrs_access( syndrome ); new_pc += 4; break;
  case 0b001001: pointer_authentication( syndrome ); new_pc += 4; break;
  case 0b001010: asm ( "brk 0b001010" ); break; // Never happens or is unimplemented
  case 0b001011: asm ( "brk 0b001011" ); break; // Never happens or is unimplemented
  case 0b001100: cp14_access_RR( syndrome ); new_pc += 4; break;
  case 0b001101: asm ( "brk 0b001101" ); break; // Never happens or is unimplemented
  case 0b001110: illegal_execution_state( syndrome ); new_pc += 4; break;
  case 0b001111: asm ( "brk 0b001111" ); break; // Never happens or is unimplemented
  case 0b010000: asm ( "brk 0b010000" ); break; // Never happens or is unimplemented
  case 0b010001: asm ( "brk 0b010001" ); break; // Never happens or is unimplemented
  case 0b010010: hvc32( syndrome ); break;
  case 0b010011: smc32( syndrome ); new_pc += 4; break;
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
  case 0b100100: stage2_data_abort( syndrome, fault_address, intermediate_physical_address ); new_pc += 4; break;
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

if (show) asm ( "svc 0" );

  return new_pc;
}

void map_page( uint64_t physical, void *virtual )
{
  PHYSICAL_MEMORY_BLOCK device_page = DRIVER_SYSTEM__get_device_page( driver_system(), NUMBER__from_integer_register( physical ) );
  DRIVER_SYSTEM__map_at( driver_system(), device_page, NUMBER__from_integer_register( (integer_register) virtual ) );
}


void entry()
{
  // Give RISC OS control over the BSC interface
  map_page( 0x3f804000, (void*) &devices.bsc0 );

  tnd = TRIVIAL_NUMERIC_DISPLAY__get_service( "Trivial Numeric Display", -1 );

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

    vm_memory_base = PHYSICAL_MEMORY_BLOCK__physical_address( riscos_memory ).r;

    Aarch64_VMSA_entry *tt = (void*) el2_tt_address.r;
    for (int i = 0; i < 512; i++) {
      tt[i] = Aarch64_VMSA_invalid;
    }

    for (int i = 0; i < 32; i++) {
      Aarch64_VMSA_entry entry = Aarch64_VMSA_block_at( vm_memory_base + (i << 21) );
      entry = Aarch64_VMSA_write_back_memory( entry );
      entry.raw |= (1 <<10); // AF
      entry = Aarch64_VMSA_L2_rwx( entry );
      tt[i] = entry;
      if (1 == (i & 1)) { // FIXME Assumes minimum cache line of 16 bytes.
        asm volatile ( "dc civac, %[va]" : : [va] "r" (&tt[i & ~1]) );
      }
    }
    DRIVER_SYSTEM__make_partner_thread( driver_system(), PHYSICAL_MEMORY_BLOCK__duplicate_to_pass_to( driver_system().r, el2_tt ) );

    uint64_t hcr2 = 0b0001110000000000000011111110110000111011;
    // uint64_t hcr2 = 0b100 0001 1100 0000 0000 0000 1111 1110 1100 0011 1011;
    // 0 1 3 4 5 10 11 13 14 15 16 17 18 19 34 35 36 42

    set_vm_system_register( HCR_EL2, hcr2 );

    // This seems to override the bits in HCR_EL2:
    uint32_t cp15_traps = 0xffff;
    //cp15_traps &= ~(1 << 7); // Don't trap cache maintenance instructions.  STOPS WORKING!
    // This stops with an undefined instruction error, internal to the NS EL1.
    // So, how about I trap undefined instructions?
    //cp15_traps &= ~(1 << 8); // Don't trap TLB maintenance instructions.
    //cp15_traps &= ~(1 << 5); // Don't trap fault status registers
    cp15_traps &= ~(1 << 6); // Don't trap fault address registers
    set_vm_system_register( HSTR_EL2, cp15_traps );
    //set_vm_system_register( HSTR_EL2, 0 );

    // Devices requiring asynchronous behaviour:
    static uint64_t __attribute__(( aligned( 16 ) )) stack[32];
    create_thread( timer_thread, stack + 32 );
    static uint64_t __attribute__(( aligned( 16 ) )) sdcard_stack[32];
    create_thread( sdcard, sdcard_stack + 32 );
    while (sdcard_thread == 0) { yield(); }

    uint64_t next_pc = 0;

//set_block( 4, 0x5555 );
    for (;;) {
      next_pc = switch_to_partner( vm_handler, next_pc );
    }
  }
}

