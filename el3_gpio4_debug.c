/* Copyright (c) 2021 Simon Willcocks */

// EL3 behaviour option:
// Initialise GPIO4 as output, and assume there's an LED attached with a simple inline resistor
// Will blink, when an HVC instruction is executed at Secure EL1 (or EL2, probably)

#include "types.h"

struct __attribute__(( packed )) gpio {
  uint32_t gpfsel[6];  // 0x00 - 0x14
  uint32_t res18;
  uint32_t gpset[2];   // 0x1c, 0x20
  uint32_t res24;
  uint32_t gpclr[2];
  uint32_t res30;     // 0x30
  uint32_t gplev[2];
  uint32_t res3c;
  uint32_t gpeds[2];   // 0x40
  uint32_t res48;
  uint32_t gpren[2];
  uint32_t res54;
  uint32_t gpfen[2];
  uint32_t res60;     // 0x60
  uint32_t gphen[2];
  uint32_t res6c;
  uint32_t gplen[2];    // 0x70
  uint32_t res78;
  uint32_t gparen[2];
  uint32_t res84;
  uint32_t gpafen[2];
  uint32_t res90;     // 0x90
  uint32_t gppud;
  uint32_t gppudclk[2];
  uint32_t resa0;
  uint32_t resa4;
  uint32_t resa8;
  uint32_t resac;
  uint32_t test;
};

static inline void led_init( uint64_t base )
{
  volatile struct gpio *g = (void *) base;
  g->gpfsel[0] = (g->gpfsel[0] & ~(7 << 12)) | (1 << 12); // GPIO pin 4

  // Never before needed, but LED not getting bright.
  g->gppud = 0;
  asm volatile ( "dsb sy" );
  for (int i = 0; i < 150; i++) { asm volatile( "mov x0, x0" ); }
  g->gppudclk[0] |= 1 << 4;
  asm volatile ( "dsb sy" );
  for (int i = 0; i < 150; i++) { asm volatile( "mov x0, x0" ); }
  g->gppud = 0;
  asm volatile ( "dsb sy" );
  g->gppudclk[0] &= ~(1 << 4);
  // End.

  asm volatile ( "dsb sy" );
}

static inline void led_off( uint64_t base )
{
  volatile struct gpio *g = (void *) base;
  g->gpclr[0] = (1 << 4);
  asm volatile ( "dsb sy" );
}

static inline void led_on( uint64_t base )
{
  volatile struct gpio *g = (void *) base;
  g->gpset[0] = (1 << 4);
  asm volatile ( "dsb sy" );
}

static inline void led_toggle( uint64_t base )
{
  volatile struct gpio *g = (void *) base;
  uint32_t on = 0 != (g->gplev[0] & (1 << 4));
  if (on)
    g->gpclr[0] = (1 << 4);
  else
    g->gpset[0] = (1 << 4);
  asm volatile ( "dsb sy" );
}

// Suitable for when caches disabled
#define LED_BLINK_TIME 0x100000

void led_blink( uint32_t base, int n ) {
  // Count the blinks! Extra short = 0, Long = 5

  if (n == 0) {
    led_on( base );
    for (uint64_t i = 0; i < LED_BLINK_TIME / 4; i++) { asm volatile ( "" ); }
    led_off( base );
    for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
  }
  else {
    while (n >= 5) {
      led_on( base );
      for (uint64_t i = 0; i < LED_BLINK_TIME * 4; i++) { asm volatile ( "" ); }
      led_off( base );
      for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
      n -= 5;
    }
    while (n > 0) {
      led_on( base );
      for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
      led_off( base );
      for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
      n --;
    }
  }
  for (uint64_t i = 0; i < 4 * LED_BLINK_TIME; i++) { asm volatile ( "" ); }
}

void blink_number( uint32_t base, uint32_t number )
{
  for (int i = 28; i >= 0; i -= 4) {
    led_blink( base, (number >> i) & 0xf );
  }
}

#define AARCH64_VECTOR_TABLE_NAME VBAR_EL3
#define AARCH64_VECTOR_TABLE_PREFIX EL3_
#define HANDLER_EL 3

#define BSOD_TOSTRING( n ) #n
#define ABSOD( n ) "0: mov x0, #0x3f200000\n\tmov x1, #0\n\tbl led_blink\n\tmov x0, #0x3f200000\n\tmov x1, #2 + " BSOD_TOSTRING( n ) "\n\tbl led_blink\n\tb 0b"
#define BSOD( n ) asm ( ABSOD( n ) );

#define AARCH64_VECTOR_TABLE_SP0_SYNC_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_SP0_IRQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_SP0_FIQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_SP0_SERROR_CODE BSOD( __COUNTER__ )

#define AARCH64_VECTOR_TABLE_SPX_SYNC_CODE asm( "0: mov x0, #0x3f200000\n\tmov x1, #0\n\tbl led_blink\n\tmov x0, #0x3f200000\n\tmrs x1, elr_el3\n\tbl blink_number\n\tmov x0, #0x3f200000\n\tmrs x1, esr_el3\n\tbl blink_number\n\tb 0b" );
#define AARCH64_VECTOR_TABLE_SPX_IRQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_SPX_FIQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_SPX_SERROR_CODE BSOD( __COUNTER__ )
// Where an SMC comes to: But, it looks like I get to VBAR_EL3_SPX_SYNC, somehow...
//#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE asm( "0: mov x0, #0x3f200000\n\tmov x1, #0\n\tbl led_blink\n\tmov x0, #0x3f200000\n\tmov x1, #18\n\tbl led_blink\n\tmov x0, #0x3f200000\n\tmrs x1, esr_el3\n\tand x1, x1, #0xffff\n\tbl led_blink\n\tb 0b" );


static uint32_t *const mapped_address = (void*) 0x0e400000;
static const uint32_t width = 1920;
static const uint32_t height = 1080;
static const uint32_t vwidth = 1920;
static const uint32_t vheight = 1080;

enum fb_colours {
  Black   = 0xff000000,
  Grey    = 0xff888888,
  Blue    = 0xff0000ff,
  Green   = 0xff00ff00,
  Red     = 0xffff0000,
  Yellow  = 0xffffff00,
  Magenta = 0xffff00ff,
  White   = 0xffffffff };

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

static inline void set_pixel( uint32_t x, uint32_t y, uint32_t colour )
{
  mapped_address[x + y * vwidth] = colour;
}

static inline void show_nibble( uint32_t x, uint32_t y, uint32_t nibble, uint32_t colour )
{
  uint32_t dx = 0;
  uint32_t dy = 0;

  for (dy = 0; dy < 8; dy++) {
    for (dx = 0; dx < 8; dx++) {
      if (0 != (bitmaps[nibble][dy] & (0x80 >> dx)))
        set_pixel( x+dx, y+dy, colour );
      else
        set_pixel( x+dx, y+dy, Black );
    }
  }
}

// static void show_word( int x, int y, uint32_t number, uint32_t colour )
void show_word( int x, int y, uint32_t number, uint32_t colour )
{
  for (int shift = 28; shift >= 0; shift -= 4) {
    show_nibble( x, y, (number >> shift) & 0xf, colour );
    x += 8;
  }
}

static void show_qword( int x, int y, uint64_t number, uint32_t colour )
{
  show_word( x, y, (uint32_t) (number >> 32), colour );
  show_word( x+66, y, (uint32_t) (number & 0xffffffff), colour );
}

/*
static void show_pointer( int x, int y, void *ptr, uint32_t colour )
{
  show_qword( x, y, ((char*)ptr - (char*)0), colour );
}
*/

static void show_page( uint32_t *number )
{
  // 4 * 16 * 16 * 4 = 4096 bytes
  for (int y = 0; y < 4*16; y++) {
    show_word( 0, y * 8 + 64, ((char *)(&number[y*16]) - (char *)0), White );
    for (int x = 0; x < 16; x++) {
      uint32_t colour = White;
      if (0 == (y & 7) || 0 == (x & 7)) colour = Green;
      show_word( x * 68 + 72, y * 8 + 64, number[x + y * 16], colour );
    }
  }
}

uint64_t __attribute__(( aligned( 16 ) )) smc_stack[64];
void showme()
{
  uint32_t *p = (void*) 0x0e400000; // Quick and dirty
  uint64_t pc;
#define SHOW( r, x, y ) { uint32_t pc; asm ( "mrs %[pc], "#r : [pc] "=r" (pc) ); show_word( x, y, pc, Yellow ); }
#define SHOWL( r, x, y ) { uint64_t pc; asm ( "mrs %[pc], "#r : [pc] "=r" (pc) ); show_qword( x, y, pc, Yellow ); }
  SHOWL( ELR_EL1, 100, 10 );
  SHOWL( FAR_EL1, 300, 10 );
  SHOW( ESR_EL1, 500, 10 );
  SHOWL( TTBR0_EL1, 600, 10 );

  SHOW( ELR_EL3, 100, 20 );
  SHOW( FAR_EL3, 200, 20 );
  SHOW( ESR_EL3, 300, 20 );

#define SHOW_REG( n ) asm ( "mov %[pc], x"#n : [pc] "=r" (pc) ); show_qword( 1600, n * 10, pc, White );
  SHOW_REG( 18 );
  SHOW_REG( 19 );
  SHOW_REG( 20 );
  SHOW_REG( 21 );
  SHOW_REG( 22 );
  SHOW_REG( 23 );
  SHOW_REG( 24 );
  SHOW_REG( 25 );
  SHOW_REG( 26 );
  SHOW_REG( 27 );
  SHOW_REG( 28 );
  SHOW_REG( 29 );
  SHOW_REG( 30 );

  uint64_t *page = (void*) 0x8000;
  // page = page[511] & ~0xfff;
  show_page( (void*) page );
  for (uint64_t i = 0; i < LED_BLINK_TIME * 400; i++) { asm volatile ( "" ); }

  for (;;)
    for (int i = 0; i < 0x30000; i+= 0x1000)
    {
      show_page( (void*) i );
      for (uint64_t i = 0; i < LED_BLINK_TIME * 10; i++) { asm volatile ( "" ); }
    }
}

#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SYNC_CODE asm( "adr x16, smc_stack+64*8\n\tmov sp, x16\n\tbl showme" );

#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_IRQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_FIQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_LOWER_AARCH64_SERROR_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SYNC_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_IRQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_FIQ_CODE BSOD( __COUNTER__ )
#define AARCH64_VECTOR_TABLE_LOWER_AARCH32_SERROR_CODE BSOD( __COUNTER__ )

#include "aarch64_vector_table.h"

#include "exclusive.h"

void setup_el3_for_reentry( int number )
{
  // Sets bits:
  //   11: ST Do not trap EL1 accesses of CNTPS_* to EL3
  //   10: RW Lower levels Aarch64
  //    9: SIF Secure Instruction Fetch (only from secure memory)
  //  ~ 7: SMD Secure Monitor Call disable
  //    5,4: res1
  asm volatile ( "\tmsr scr_el3, %[bits]" : : [bits] "r" (0b00000000111000110000) );

  asm volatile ( "\tmsr VBAR_EL3, %[table]\n" : : [table] "r" (VBAR_EL3) );

  static uint64_t volatile gpio4_initialised = 0;
  if (number == 0) {
    led_init( 0x3f200000 );
    led_on( 0x3f200000 );

    gpio4_initialised = 1;
  }

  // Don't return until initialised, in case we want to use it immediately
  while (gpio4_initialised != 1) {}
}
