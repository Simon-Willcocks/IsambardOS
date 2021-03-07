/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer, GPU timers, and GPIO.
//
// Current limitations:.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice with anyone else using the mailbox

#include "devices.h"

static inline void gpio_set_alternate_function_bits( int gpio, int code )
{
  int offset = 3 * (gpio % 10);
  uint32_t volatile *reg = &devices.gpio.GPFSEL[gpio / 10];
  *reg = (*reg & ~(7 << offset)) | ((code & 7) << offset);
}

void gpio_set_as_input( int gpio )
{
  gpio_set_alternate_function_bits( gpio, 0 );
}

void gpio_set_as_output( int gpio )
{
  gpio_set_alternate_function_bits( gpio, 1 );
}

void gpio_set_function( int gpio, int alt )
{
  static const uint32_t alts[6] = { 4, 5, 6, 7, 3, 2 };
  gpio_set_alternate_function_bits( gpio, alts[alt] );
}

uint64_t gpio_read()
{
  uint64_t result;
  asm volatile ( "orr %[result], %[low], %[high], LSL#32" : [result] "=r" (result) : [low] "r" (devices.gpio.GPLEV[0]), [high] "r" (devices.gpio.GPLEV[1]) );
  return result;
}

uint64_t requesting_pull_up = 0;
uint64_t requesting_pull_down = 0;

uint64_t currently_pull_up   = 0b111111110000000001110000000000000000000000000111111111ull;
uint64_t currently_pull_down = 0b000000000011111110001111001111111111111111111000000000ull;

void gpio_request_pull_up( int gpio )
{
  requesting_pull_up = (1ull << gpio);
}

void gpio_request_pull_down( int gpio )
{
  requesting_pull_down = (1ull << gpio);
}

static inline uint32_t low_word( uint64_t v )
{
  return v & 0xffffffff;
}

static inline uint32_t high_word( uint64_t v )
{
  return v >> 32;
}

void gpio_set_pull_up_down()
{
  // This is strange.
  // Write to GPPUD 1 = down, 2 = up
  // "Wait 150 cycles" (at what frequency?)
  // "Write to GPPUDCLK0/1" to set each set bit to the chosen mode
  // "Wait 150 cycles" (at what frequency?)
  // Write 0 to GPPUD to remove the control signal
  // Write (0?) to GPPUDCLK0/1 to remove the clock
  uint64_t change_up = requesting_pull_up & ~currently_pull_up;
  uint64_t change_down = requesting_pull_down & ~currently_pull_down;

  if (change_up != 0) {
    devices.gpio.GPPUD = 2;
    yield();
    yield();
    yield();
    if (low_word( change_up ) != 0) devices.gpio.GPPUDCLK[0] = low_word( change_up );
    if (high_word( change_up ) != 0) devices.gpio.GPPUDCLK[1] = high_word( change_up );
    yield();
    yield();
    yield();
    devices.gpio.GPPUD = 0;
    devices.gpio.GPPUDCLK[0] = 0;
    devices.gpio.GPPUDCLK[1] = 0;

    currently_pull_up = currently_pull_up | requesting_pull_up;
    currently_pull_down = currently_pull_down & ~requesting_pull_up;
  }

  if (change_down != 0) {
    devices.gpio.GPPUD = 1;
    yield();
    yield();
    yield();
    if (low_word( change_down ) != 0) devices.gpio.GPPUDCLK[0] = low_word( change_down );
    if (high_word( change_down ) != 0) devices.gpio.GPPUDCLK[1] = high_word( change_down );
    yield();
    yield();
    yield();
    devices.gpio.GPPUD = 0;
    devices.gpio.GPPUDCLK[0] = 0;
    devices.gpio.GPPUDCLK[1] = 0;

    currently_pull_down = currently_pull_down | requesting_pull_down;
    currently_pull_up = currently_pull_up & ~requesting_pull_down;
  }

  requesting_pull_up = 0;
  requesting_pull_down = 0;
}

void gpio_set_detect_high( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPHEN[reg] = devices.gpio.GPHEN[reg] | (1 << offset);
}

void gpio_set_detect_low( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPLEN[reg] = devices.gpio.GPLEN[reg] | (1 << offset);
}

void gpio_set_detect_rising( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPAREN[reg] = devices.gpio.GPAREN[reg] | (1 << offset);
}

void gpio_set_detect_falling( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPAFEN[reg] = devices.gpio.GPAFEN[reg] | (1 << offset);
}

void gpio_clear_detect_high( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPHEN[reg] = devices.gpio.GPHEN[reg] & ~(1 << offset);
}

void gpio_clear_detect_low( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPLEN[reg] = devices.gpio.GPLEN[reg] & ~(1 << offset);
}

void gpio_clear_detect_rising( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPAREN[reg] = devices.gpio.GPAREN[reg] & ~(1 << offset);
}

void gpio_clear_detect_falling( int gpio )
{
  int reg = gpio / 32;
  int offset = gpio & 31;
  devices.gpio.GPAFEN[reg] = devices.gpio.GPAFEN[reg] & ~(1 << offset);
}

