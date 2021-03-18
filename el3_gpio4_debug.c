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

uint64_t __attribute__(( aligned( 16 ) )) smc_stack[64];
void showme()
{
  uint64_t pc;
  asm ( "mrs %[pc], ELR_EL1" : [pc] "=r" (pc) );
  for (;;)
  blink_number( 0x3f200000, pc );
  
  uint32_t volatile *base = (uint32_t*) 0xe450;
  while (base[5] == (2 << 20)) {
    
  }
  uint32_t volatile *p = (uint32_t*) (uint64_t) base[5];
  for (int i = 0; i < 10000; i++) p[i] = 0xffffffff;
  for (;;) {}
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
