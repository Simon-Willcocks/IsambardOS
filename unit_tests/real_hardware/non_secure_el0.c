/* Copyright (c) Simon Willcocks 2021 */

// Objective: Run 32-bit code at non-secure el0.
// Reason: Attempts in the main code have resulted in an ESR_EL3 value of 0x3A000000,
// Illegal Execution state, despite being accepted by qemu.

// No MMU needed.

asm ( ".section .init"
    "\n.global  _start"
    "\n.type    _start, %function"
    "\n_start:"
    "\n\tmrs     x0, mpidr_el1"
    "\n\ttst     x0, #0x40000000"
    "\n\tand     x1, x0, #0xff"
    "\n\tcsel    x1, x1, xzr, eq" // core
    "\n\tcbnz    x1, other_cores"
    "\n\tadr     x0, free_page"
    "\n\tadd     sp, x0, #4096"
    "\n\tb     enter"
    "\nother_cores: wfi"
    "\n\tb other_cores"
    "\n.align 10" );

/*
Build command:
aarch64-none-elf-gcc -fno-zero-initialized-in-bss non_secure_el0.c -I ../../include/ -nostdlib -nostartfiles -o non_secure_el0.o -T ld.script  && aarch64-none-elf-objcopy non_secure_el0.o -O binary kernel8.img

-fno-zero-initialized-in-bss ensures that zeroed bytes are inserted into the binary file, rather than expecting them to be initialised in code.

 */

typedef unsigned long long uint64_t;
typedef unsigned uint32_t;

uint64_t __attribute__(( aligned( 4096 ) )) free_page[512] = {};

/////////////////////////////// Simple and obsolete FB code ///////////////////

struct __attribute__(( packed, aligned( 64 ) )) fb_info {
  uint32_t pwidth;
  uint32_t pheight;
  uint32_t vwidth;
  uint32_t vheight;
  uint32_t out_pitch;
  uint32_t depth;
  uint32_t xoffset;
  uint32_t yoffset;
  uint32_t out_fb; // 32-bit pointer, don't use pointer type in A64
  uint32_t out_size;
};

uint32_t *fb_memory( const struct fb_info *p );
uint32_t fb_size( const struct fb_info *p );
uint32_t fb_pitch( const struct fb_info *p );

// Ignoring the requested sizes, going for 1080p
void initialise_fb( struct fb_info volatile *info )
{
  struct __attribute__(( packed )) BCM_mailbox {
    uint32_t value; // Request or Response, depending if from or to ARM,
               // (Pointer & 0xfffffff0) | Channel 0-15
    uint32_t res1;
    uint32_t res2;
    uint32_t res3;
    uint32_t peek;  // Presumably doesn't remove the value from the FIFO
    uint32_t sender;// ??
    uint32_t status;// bit 31: Tx full, 30: Rx empty
    uint32_t config;
  } volatile *mailbox = (void*) 0x3f00b880; // 0x3f000000 + 0xb880
  // ARM may read mailbox 0, write mailbox 1.

    static uint32_t __attribute__(( aligned( 16 ) )) request[26] = { sizeof( request ), 0, // Message buffer size, request
	    // Tags: Tag, buffer size, request code, buffer
	    0x00040001, // Allocate buffer
	    8, 0, 2*1024*1024, 0, // Size, Code, In: Alignment, Out: Base, Size
	    0x00048003, // Set physical (display) width/height
	    8, 0, 1920, 1080,
	    0x00048004, // Set virtual (buffer) width/height
	    8, 0, 1920, 1080,
	    0x00048005, // Set depth
	    4, 0, 32,
	    0x00048006, // Set pixel order
	    4, 0, 1,    // 0 = BGR, 1 = RGB
            0 }; // End of tags tag

  request[0] = sizeof( request );

  while (mailbox[1].status & 0x80000000) { // Tx full
    asm volatile( "dsb sy" );
  }

  // Channel 8: Request from ARM for response by VC
  mailbox[1].value = 0x8 | ((char*)request-(char*)0);

  asm volatile( "dsb sy" );
  while (mailbox[0].status & 0x40000000) { // Rx empty
    asm volatile( "dsb sy" );
  }
  uint32_t response = mailbox[0].value;
  asm ( "" : : "r" (response) ); // Remove warning

  info->out_fb = request[5] & 0x3fffffff;
  info->out_size = request[6];
}

inline uint32_t *fb_memory( const struct fb_info *p )
{
  return (uint32_t *) (uint64_t) p->out_fb;
}

inline uint32_t fb_size( const struct fb_info *p )
{
  return p->out_size;
}

inline uint32_t fb_pitch( const struct fb_info *p )
{
  return p->out_pitch;
}

static const unsigned char bitmaps[16][8] = {
  {
  0b00111100,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01100110,
  0b00111100,
  0b00000000
  },{
  0b00011100,
  0b00111100,
  0b00001100,
  0b00001100,
  0b00001100,
  0b00001100,
  0b00011110,
  0b00000000
  },{
  0b00111100,
  0b01100110,
  0b00001100,
  0b00011000,
  0b00110000,
  0b01111110,
  0b01111110,
  0b00000000
  },{
  0b00111100,
  0b01100110,
  0b00000110,
  0b00011110,
  0b00000110,
  0b01100110,
  0b00111100,
  0b00000000
  },{
  0b00011000,
  0b00110000,
  0b01100000,
  0b01101100,
  0b01111110,
  0b00001100,
  0b00001100,
  0b00000000
  },{
  0b01111110,
  0b01100000,
  0b01100000,
  0b01111100,
  0b00000110,
  0b01100110,
  0b00111100,
  0b00000000
  },{
  0b00111100,
  0b01100110,
  0b01100000,
  0b01111100,
  0b01100110,
  0b01100110,
  0b00111100,
  0b00000000
  },{
  0b01111110,
  0b00000110,
  0b00001100,
  0b00011000,
  0b00110000,
  0b01100000,
  0b01100000,
  0b00000000
  },{
  0b00111100,
  0b01100110,
  0b01100110,
  0b00111100,
  0b01100110,
  0b01100110,
  0b00111100,
  0b00000000
  },{
  0b00111100,
  0b01100110,
  0b01100110,
  0b00111110,
  0b00000110,
  0b01100110,
  0b00111000,
  0b00000000
  },{
  0b00111100,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01111110,
  0b01100110,
  0b01100110,
  0b00000000
  },{
  0b01111000,
  0b01100110,
  0b01100110,
  0b01111100,
  0b01100110,
  0b01100110,
  0b01111000,
  0b00000000
  },{
  0b00111100,
  0b01100110,
  0b01100000,
  0b01100000,
  0b01100000,
  0b01100110,
  0b00111100,
  0b00000000
  },{
  0b01111000,
  0b01101100,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01101100,
  0b01111000,
  0b00000000
  },{
  0b01111110,
  0b01100000,
  0b01100000,
  0b01111000,
  0b01100000,
  0b01100000,
  0b01111110,
  0b00000000
  },{
  0b01111110,
  0b01100000,
  0b01100000,
  0b01111000,
  0b01100000,
  0b01100000,
  0b01100000,
  0b00000000
  }
};

static inline void set_pixel( const struct fb_info *fb, uint32_t x, uint32_t y, uint32_t colour )
{
  fb_memory( fb )[x + y * fb->vwidth] = colour;
}

static inline void show_nibble( const struct fb_info *fb, uint32_t x, uint32_t y, uint32_t nibble, uint32_t colour )
{
  uint32_t dx = 0;
  uint32_t dy = 0;

  for (dy = 0; dy < 8; dy++) {
    for (dx = 0; dx < 8; dx++) {
      if (0 != (bitmaps[nibble][dy] & (0x80 >> dx)))
        set_pixel( fb, x+dx, y+dy, colour );
      else
        set_pixel( fb, x+dx, y+dy, 0xff000000 );
    }
  }
}

static void __attribute__(( noinline )) show_byte( const struct fb_info *fb, int x, int y, uint32_t number, uint32_t colour )
{
  show_nibble( fb, x, y, (number >> 8) & 0xf, colour );
  show_nibble( fb, x+8, y, number & 0xf, colour );
}

static void __attribute__(( noinline )) show_word( const struct fb_info *fb, int x, int y, uint32_t number, uint32_t colour )
{
  for (int shift = 28; shift >= 0; shift -= 4) {
    show_nibble( fb, x, y, (number >> shift) & 0xf, colour );
    x += 8;
  }
}

static void __attribute__(( noinline )) show_qword( const struct fb_info *fb, int x, int y, uint64_t number, uint32_t colour )
{
  show_word( fb, x, y, (uint32_t) (number >> 32), colour );
  show_word( fb, x+66, y, (uint32_t) (number & 0xffffffff), colour );
}

static void show_pointer( const struct fb_info *fb, int x, int y, void *ptr, uint32_t colour )
{
  show_qword( fb, x, y, ((char*)ptr - (char*)0), colour );
}

enum fb_colours {
  Black = 0xff000000,
  Grey  = 0xff888888,
  Blue  = 0xff0000ff,
  Green = 0xff00ff00,
  Red   = 0xffff0000,
  Yellow= 0xffffff00,
  White = 0xffffffff };

static void __attribute__(( noinline )) show_page( const struct fb_info *fb, uint32_t *number )
{
  // 4 * 16 * 16 * 4 = 4096 bytes
  for (int y = 0; y < 4*16; y++) {
    show_word( fb, 0, y * 8 + 64, ((char *)(&number[y*16]) - (char *)0), White );
    for (int x = 0; x < 16; x++) {
      uint32_t colour = White;
      if (0 == (y & 7) || 0 == (x & 7)) colour = Green;
      show_word( fb, x * 68 + 72, y * 8 + 64, number[x + y * 16], colour );
    }
  }
}


/////////////////////////////////// TEST CODE  ///////////////////////////////////////////

static struct fb_info fb = { 1920, 1080, 1920, 1080, 0, 32, 0, 0, 0, 0 };

void c_bsod();

#define AARCH64_VECTOR_TABLE_SP0_SYNC_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_SP0_IRQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_SP0_FIQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_SP0_SERROR_CODE asm ( "bl c_bsod" );

#define AARCH64_VECTOR_TABLE_SPX_SYNC_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_SPX_IRQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_SPX_FIQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_SPX_SERROR_CODE asm ( "bl c_bsod" );

#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_IRQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_FIQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SERROR_CODE asm ( "bl c_bsod" );

#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SYNC_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_IRQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_FIQ_CODE asm ( "bl c_bsod" );
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SERROR_CODE asm ( "bl c_bsod" );

#define AARCH64_VECTOR_TABLE_NAME VBAR_EL1
#include "aarch64_vector_table.h"
#undef AARCH64_VECTOR_TABLE_NAME

#define AARCH64_VECTOR_TABLE_NAME VBAR_EL2
#include "aarch64_vector_table.h"
#undef AARCH64_VECTOR_TABLE_NAME

#define AARCH64_VECTOR_TABLE_NAME VBAR_EL3
#include "aarch64_vector_table.h"
#undef AARCH64_VECTOR_TABLE_NAME


#define get_system_reg( name, value ) asm ( "mrs %[v], "#name : [v] "=&r" (value) )
#define set_system_reg( name, value ) asm ( "msr "#name", %[v]" : : [v] "r" (value) )
#define modify_system_reg( name, bits, set ) asm ( "mrs x4, "#name"\nbic x4, x4, %[b]\norr x4, x4, %[s]\nmsr "#name", x4" : : [b] "r" (bits), [s] "r" (set) : "x4" )

void example_a64();
uint32_t example_a32[] = { 0xeafffffe }; // Infinite loop

void show_regs( int x )
{
  int y = 120;
  static uint64_t last_value[70] = { 0 };

#define show( reg ) { uint64_t r; asm ( "mrs %[r], "#reg : [r] "=r" (r) ); show_qword( &fb, x, y, r, (r == last_value[y/20]) ? White : Red ); last_value[y/20] = r; y += 20; }

// Initial values:
   show( vmpidr_el2 );     // 00000000 80000000
   show( vpidr_el2 );      // 00000000 410fd034
   y += 10;
   show( hcr_el2 );        // 00000000 00000002
   show( vttbr_el2 );      // 000080b2 d3ff7fd0
   show( vtcr_el2 );       // 00000000 8000695f
   y += 10;
   show( vbar_el1 );       // 00000000 00000000
   show( vbar_el2 );       // 00000000 00000000
   show( vbar_el3 );       // 00000000 00000000
   y += 10;
   show( far_el1 );        // r
   show( far_el2 );        // r
   show( far_el3 );        // r
   y += 10;
   show( elr_el1 );        // r
   show( elr_el2 );        // r
   show( elr_el3 );        // r
   y += 10;
   show( esr_el1 );        // 00000000 6fc3f76e
   show( esr_el2 );        // 00000000 cdf93fee
   show( esr_el3 );        // 00000000 6fdbffdd
   y += 10;
   show( spsr_el1 );       // 00000000 000001cd
   show( spsr_el2 );       // 00000000 4a292aeb
   show( spsr_el3 );       // 00000000 000001cd
   y += 10;
   show( sctlr_el1 );      // 00000000 00c50838
   show( sctlr_el2 );      // 00000000 30c50838
   show( sctlr_el3 );      // 00000000 00c50838
   y += 10;
   show( hpfar_el2 );      // 00000000 ef5df7a0
   show( ifsr32_el2 );     // 00000000 00001635
   show( isr_el1 );        // 00000000 00000000
   show( scr_el3 );        // 00000000 00000000
   y += 10;
   show( sp_el0 );         // r
   show( sp_el1 );         // r
   show( sp_el2 );         // r
   y += 10;
   show( cntkctl_el1 );    // 00000000 00000000
   show( csselr_el1 );     // 00000000 00000000
   show( mair_el1 );       // 44e048e0 00098aa4 (random?)
   show( tcr_el1 );        // 00000000 00000000
   show( ttbr0_el1 );      // 515d40d3 e98bd983 r
   show( ttbr1_el1 );      // 97fd81f3 f8f86f78 r
   show( hstr_el2 );       // 00000000 00000000
}

int catch_el( uint64_t caller )
{
  if (caller >= (uint64_t) VBAR_EL3
   && caller <  0x800 + (uint64_t) VBAR_EL3) return 3;
  if (caller >= (uint64_t) VBAR_EL2
   && caller <  0x800 + (uint64_t) VBAR_EL2) return 2;
  if (caller >= (uint64_t) VBAR_EL1
   && caller <  0x800 + (uint64_t) VBAR_EL1) return 1;
  return 0;
}

void c_bsod()
{
  static int volatile i = 0;
  uint64_t caller;
  asm ( "mov %[caller], x30" : [caller] "=r" (caller) );
  int el = catch_el( caller );
  show_word( &fb, i * 400 + 80, 100 - 10 * el, caller - 4, Green );
  show_byte( &fb, i * 400 + 160, 100 - 10 * el, el, Green );

  // If the instruction is caught at a lower el, pass it up until we get to EL3
  // Until we're at el3, show_regs won't work
  if (el == 1 || el == 2) {
    asm( "smc 1" );
  }

  show_regs( i * 400 + 80 );

  i ++;

  // If we want non-secure modes:
  //   SCR_EL3.NS must be 1
  // If we want non-secure el0 and el1:
  //   HCR_EL2.TGE must be 0 D1-1885
  // Dropping to a 64-bit mode, the code must be:
  //   <el> 0 0 or <el> 0 1, for el != 0
  // AND SCR_EL3.RW must be 1
  // HCR_EL2.RW, too, if the mode is non-secure el1 or 0

  enum { NO_INTERRUPTS = 0b111000000 };
  enum {
    M32_User = 0b10000 | NO_INTERRUPTS,
    M32_FIQ = 0b10001 | NO_INTERRUPTS,
    M32_IRQ = 0b10010 | NO_INTERRUPTS,
    M32_Svc = 0b10011 | NO_INTERRUPTS,
    M32_Monitor = 0b10110 | NO_INTERRUPTS,
    M32_Abort = 0b10111 | NO_INTERRUPTS,
    M32_Hyp = 0b11010 | NO_INTERRUPTS,
    M32_Undef = 0b11011 | NO_INTERRUPTS,
    M32_System = 0b11111 | NO_INTERRUPTS,

    EL0h = 0b00000 | NO_INTERRUPTS,
    EL0t = 0b00001 | NO_INTERRUPTS, // Invalid mode, included  for completeness
    EL1h = 0b00100 | NO_INTERRUPTS,
    EL1t = 0b00101 | NO_INTERRUPTS,
    EL2h = 0b01000 | NO_INTERRUPTS,
    EL2t = 0b01001 | NO_INTERRUPTS,
    EL3h = 0b01100 | NO_INTERRUPTS,
    EL3t = 0b01101 | NO_INTERRUPTS
  };

  int started = 0;
  const int works = (0==0);
  // All tests preceded by if (!works) have been successful, at least once!

  if (!works)
  {
    static int done = 0;
    if (!started && !done)
    {
      set_system_reg( elr_el3, example_a64 );
      set_system_reg( spsr_el3, EL1h );
      set_system_reg( hcr_el2, 0x0 );
      set_system_reg( sp_el0, 0x100000 );
      modify_system_reg( scr_el3, 1, 0 ); // Clear NS bit
      done = 1; started = 1;
    }
  }

  if (!works)
  {
    static int done = 0;
    if (!started && !done)
    {
      set_system_reg( elr_el3, example_a64 );
      set_system_reg( spsr_el3, EL2h );
      set_system_reg( sp_el2, 0x200000 );
      modify_system_reg( scr_el3, 1, 1 ); // Set NS bit
      set_system_reg( hcr_el2, 0x80000000 );
      done = 1; started = 1;
    }
  }

  // if (!works)
  {
    static int done = 0;
    if (!started && !done)
    {
      set_system_reg( elr_el2, example_a32 );
      set_system_reg( spsr_el2, M32_Svc );
      set_system_reg( sp_el2, 0x200000 );
      set_system_reg( sp_el1, 0x300000 );
      set_system_reg( sp_el0, 0x400000 );
      modify_system_reg( scr_el3, 1, 1 ); // Set NS bit
      modify_system_reg( hcr_el2, (1 << 31), (1 << 31) ); // Set RW (32-bit)
      done = 1; started = 1;
      // Drop to EL2 before dropping to 32-bit SVC (doesn't help!)
      extern void an_eret();
      set_system_reg( elr_el3, an_eret );
      set_system_reg( spsr_el3, EL2h );
      // Can't show all regs at EL2, so do it here
      show_regs( i * 400 - 100 );
      // This eret drops to EL2 at the location of the eret, which 
      // then drops to the lower level
      asm ( "an_eret: eret" );
    }
  }

  if (started) {
    show_regs( i * 400 - 100 );
    asm ( "mov sp, %[SP]\neret" : : [SP] "r" (&free_page[512]) );
  }

  for (;;) { asm ( "wfi" ); }
}

void example_a64()
{
  asm ( "smc 0" );
  for (;;) {}
}

void __attribute__(( noreturn, noinline )) enter( uint64_t *core_mem, uint32_t core )
{
  // Running at EL3, no MMU

  initialise_fb( &fb );

  // Note: my display doesn't show the outer pixels, keep output in the middle

  // Pure regs, before any setup
  show_regs( 80 );

  asm volatile ( "  msr VBAR_EL3, %[table]\n" : : [table] "r" (VBAR_EL3) );
  asm volatile ( "  msr VBAR_EL2, %[table]\n" : : [table] "r" (VBAR_EL2) );
  asm volatile ( "  msr VBAR_EL1, %[table]\n" : : [table] "r" (VBAR_EL1) );

  // Make lower levels 64-bit
  modify_system_reg( scr_el3, (1 << 10), (1 << 10) );

  c_bsod();

  for (;;) { asm ( "wfi" ); }
  __builtin_unreachable();
}
