/* Copyright (c) 2021 Simon Willcocks */

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

void led_init( uint64_t base )
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

void led_off( uint64_t base )
{
  volatile struct gpio *g = (void *) base;
  g->gpclr[0] = (1 << 4);
  asm volatile ( "dsb sy" );
}

void led_on( uint64_t base )
{
  volatile struct gpio *g = (void *) base;
  g->gpset[0] = (1 << 4);
  asm volatile ( "dsb sy" );
}

