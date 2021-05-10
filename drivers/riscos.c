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

static const NUMBER disc_address = { .r = 0x363c0 }; // RISCOS.IMG block number
static uint32_t const expected_crc = 0x0c84b58f;


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
  arm_code[i++] = 0xe587601c; // 	str	r6, [r7, #28]
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

static void cp15_access( uint32_t syndrome )
{
  copro_syndrome cp = { .raw = syndrome };
static int y = 50;
static int x = 1000;

TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x ), N( y ), N( cp.Opc1 ), N( 0xff0ff0f0 ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+10 ), N( y ), N( cp.CRn ), N( 0xff0ff0f0 ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+20 ), N( y ), N( cp.CRm ), N( 0xff0ff0f0 ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+30 ), N( y ), N( cp.Opc2 ), N( 0xff0ff0f0 ) );
TRIVIAL_NUMERIC_DISPLAY__show_4bits( tnd, N( x+40 ), N( y ), N( cp.is_read ), N( 0xff0ff0f0 ) );
TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( x+50 ), N( y ), N( cp.Rt ), N( 0xff0ff0f0 ) );
y+=10;
if (y > 1000) {
  y = 50;
  x += 100;
}
  switch (syndrome & 0xffc1e) { // CRm, CRn, Opc1, Opc2
  case 0x00000: READ_ONLY( 0x410fb767 ); break; // midr ~ ARM1176 - does VPIDR_EL2 ever get used?
  case 0xa0000: READ_ONLY( 0x80000f00 ); break; // mpidr
  case 0x01c0a: WRITE_ONLY; break; // G7.2.76    ICIALLU, Instruction Cache Invalidate All to PoU
  case 0x0200e: WRITE_ONLY; break; // G7.2.130   TLBIALL, TLB Invalidate All
  case 0xc1c0a: WRITE_ONLY; break; // G7.2.21    BPIALL, Branch Predictor Invalidate All
  case 0x81c14: WRITE_ONLY; asm ( "dsb sy" ); break; // G7.2.29    CP15DSB, Data Synchronization Barrier System instruction
  case 0x81c0a: WRITE_ONLY; asm ( "isb sy" ); break; // G7.2.30    CP15ISB, Instruction Synchronization Barrier System instruction
  case 0x20000: { READ_ONLY( 0x1d152152 ); } break; // G7.2.34    CTR, Cache Type Register
  case 0x24000: WRITE_ONLY; break; // G7.2.26    CLIDR, Cache Level ID Register
  case 0x01c0e: WRITE_ONLY; asm ( "isb sy\ndsb sy" ); break; // ARM DDI 0360F Invalidate Both Caches. Also flushes the branch target cache
  case 0x00400: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( SCTLR_EL1, v ); } } break; // SCTLR
  case 0xa1c14: WRITE_ONLY; asm ( "dmb sy" ); break; // G7.2.28         CP15DMB, Data Memory Barrier System instruction
  case 0x01c1c: WRITE_ONLY; asm ( "dmb sy" ); break; // Data Memory Barrier
  case 0x00c00: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( DACR32_EL2, v ); } } break; // G7.2.35 DACR, Domain Access Control Register
  case 0x40800: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( TCR_EL1, v ); } }; break; // Translation Table Base Control Register
  case 0x20002: READ_ONLY( 1 ); break; // ID_PFR1
  case 0x00800: { READ_WRITE( 0 ); if (!cp.is_read) { set_vm_system_register( TTBR0_EL1, v ); } }; break; // Translation Table Base Register 0

  default: asm ( "brk 65" );
  }
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
static void hvc32( uint32_t syndrome ) { asm ( "brk 1" ); }
static void smc32( uint32_t syndrome ) { asm ( "brk 1" ); }

static void respond_to_tag_request( uint32_t *request )
{
  uint32_t *p = request;

  // Always assumes correct use
  p[1] = 0x80000000;
  p+=2;
  while (p[0] != 0) {
    // Always a valid request and response...
    // p[0] = tag
    // p[1] = buffer size (bytes)
    // p[2] = request code 0, response code 0x80000000 | length
    // p[3] = first word
    p[2] = p[1] | (1<<31);
    switch (p[0]) {
    case 0x00010001: // Board model
      p[3] = 0x2a2a2a2a; // Nobody cares, afaics!
      break;
    case 0x00010002: // Board revision
      p[3] = 13; // Model B+, 512MB https://elinux.org/RPi_HardwareHistory#Board_Revision_History NOT NewScheme!
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
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 40 ), N( p[0] ), N( 0xff0ff0f0 ) );
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 50 ), N( p[3] ), N( 0xff0ff0f0 ) );
        asm ( "brk 1" );
      }
      break;
    case 0x00038002: // Set clock rate
      switch (p[3]) {
      case 2: break; // UART
      default:
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 40 ), N( p[0] ), N( 0xff0ff0f0 ) );
        TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 50 ), N( p[3] ), N( 0xff0ff0f0 ) );
        asm ( "brk 1" );
      }
      break;
    default: // Unknown
      TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1800 ), N( 40 ), N( p[0] ), N( 0xfffff0f0 ) );
      asm ( "brk 1" );
      break;
    }
    p += 3 + p[1]/4;
  }
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

  switch (fault_address) {
  case BASE | 0x100020: // Power management, reset status
    {
      if (!s.WnR) {
        set_partner_register( s.SRT, 1 << 12 ); // Had power on reset
      }
      else { asm ( "brk 1" ); }
      break;
    }
  case BASE | 0x00b880: // Mailbox 0 data, return the last request that we processed when it was received
    {
      if (!s.WnR) {
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

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1200 ), N( 400 ), NUMBER__from_integer_register( mailbox_request ), N( 0xff00ffff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1200 ), N( 410 ), NUMBER__from_integer_register( s.SRT ), N( 0xff00ffff ) );

      if (s.WnR) {
        mailbox_request = get_partner_register( s.SRT );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 800 ), N( 420 ), NUMBER__from_integer_register( mailbox_request ), N( 0xff00ffff ) );
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
      break;
    }
  default:
    asm ( "brk 1" );
  }
}

uint64_t vm_handler( uint64_t pc, uint64_t syndrome, uint64_t fault_address, uint64_t intermediate_physical_address )
{
  static int count = 0;
  uint64_t new_pc = pc; // Retry instruction by default

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 200 ), N( syndrome ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 210 ), N( fault_address ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 220 ), N( intermediate_physical_address ), N( 0xffff8080 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 230 ), N( pc ), N( 0xfffff0f0 ) );
  TRIVIAL_NUMERIC_DISPLAY__show_64bits( tnd, N( 1400 ), N( 240 ), N( count++ ), N( 0xfffff0f0 ) );

  for (int i = 0; i < 31; i++) {
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 260+11*i ), N( get_partner_register( i ) ), N( 0xffff8080 ) );
  }

asm ( "svc 0" );
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
  case 0b010010: hvc32( syndrome ); new_pc += 4; break;
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
      if (1 == (i & 1)) { // FIXME Assumes minimum cache line of 16 bytes.
        asm volatile ( "dc civac, %[va]" : : [va] "r" (&tt[i & ~1]) );
      }
    }
    DRIVER_SYSTEM__make_partner_thread( driver_system(), PHYSICAL_MEMORY_BLOCK__duplicate_to_pass_to( driver_system().r, el2_tt ) );

    uint64_t next_pc = 0;
int events = 0;
    for (;;) {
      next_pc = switch_to_partner( vm_handler, next_pc );
if (++events > 0x48)
sleep_ms( 5000 );
    }
  }
}

