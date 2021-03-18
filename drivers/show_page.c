/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice and yield while waiting for mailbox response
// Doesn't play nice with anyone else using the mailbox

#include "drivers.h"

ISAMBARD_INTERFACE( FRAME_BUFFER )
#include "interfaces/client/FRAME_BUFFER.h"

static const uint32_t width = 1920;
static const uint32_t height = 1080;
static const uint32_t vwidth = width;
static const uint32_t vheight = height;
static integer_register const mapped_address = (2 << 20);

enum fb_colours {
  Black = 0xff000000,
  Grey  = 0xff888888,
  Blue  = 0xff0000ff,
  Green = 0xff00ff00,
  Red   = 0xffff0000,
  Yellow= 0xffffff00,
  White = 0xffffffff };

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
  if (y >= vheight) {} // FIXME
  ((uint32_t * const) mapped_address)[x + y * vwidth] = colour;
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

static void show_word( int x, int y, uint32_t number, uint32_t colour )
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

static void show_page( uint32_t *number, uint32_t label )
{
  // 4 * 16 * 16 * 4 = 4096 bytes
  for (int y = 0; y < 4*16; y++) {
    show_word( 0, y * 8 + 64, label + y*64, White );
    for (int x = 0; x < 16; x++) {
      uint32_t colour = White;
      if (0 == (y & 7) || 0 == (x & 7)) colour = Green;
      show_word( x * 68 + 72, y * 8 + 64, number[x + y * 16], colour );
    }
  }
}

PHYSICAL_MEMORY_BLOCK page_to_display = { 0 };
uint32_t page_start = 0;

uint64_t tnd_stack_lock = 0;
struct {
  uint64_t stack[64];
} __attribute__(( aligned ( 16 ) )) tnd_stack = { { 0x6666666666 } };

typedef NUMBER TND;

ISAMBARD_INTERFACE( TRIVIAL_NUMERIC_DISPLAY )

#include "interfaces/provider/TRIVIAL_NUMERIC_DISPLAY.h"

ISAMBARD_TRIVIAL_NUMERIC_DISPLAY__SERVER( TND )
ISAMBARD_PROVIDER( TND, AS_TRIVIAL_NUMERIC_DISPLAY( TND ) )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( TND, tnd_stack_lock, tnd_stack, 64 * 8 )

void TND__TRIVIAL_NUMERIC_DISPLAY__show_4bits( TND o, NUMBER x, NUMBER y, NUMBER value, NUMBER colour )
{
  o = o;
  show_nibble( x.r, y.r, value.r & 0xf, colour.r );
}

void TND__TRIVIAL_NUMERIC_DISPLAY__show_8bits( TND o, NUMBER x, NUMBER y, NUMBER value, NUMBER colour )
{
  o = o;
  show_nibble( x.r+8, y.r, value.r & 0xf, colour.r );
  show_nibble( x.r, y.r, (value.r >> 4) & 0xf, colour.r );
}

void TND__TRIVIAL_NUMERIC_DISPLAY__show_32bits( TND o, NUMBER x, NUMBER y, NUMBER value, NUMBER colour )
{
  o = o;
  show_word( x.r, y.r, value.r, colour.r );
}

void TND__TRIVIAL_NUMERIC_DISPLAY__show_64bits( TND o, NUMBER x, NUMBER y, NUMBER value, NUMBER colour )
{
  o = o;
  show_qword( x.r, y.r, value.r, colour.r );
}

extern uint32_t devices;

void TND__TRIVIAL_NUMERIC_DISPLAY__set_page_to_show( TND o, PHYSICAL_MEMORY_BLOCK page, NUMBER start )
{
  o = o;

  DRIVER_SYSTEM__map_at( driver_system(), page, NUMBER_from_integer_register( (uint64_t) &devices ) );
  page_to_display = page;
  page_start = start.r;
}

void TND__TRIVIAL_NUMERIC_DISPLAY__show_page( TND o )
{
  o = o;

  show_page( &devices, page_start );
}

void entry()
{
  FRAME_BUFFER fb = FRAME_BUFFER__get_service( "Frame Buffer", 0 );

  PHYSICAL_MEMORY_BLOCK screen_page = FRAME_BUFFER__get_frame_buffer( fb );
  DRIVER_SYSTEM__map_at( driver_system(), screen_page, NUMBER_from_integer_register( mapped_address ) );

  TND tnd = {};
  TND_TRIVIAL_NUMERIC_DISPLAY_register_service( "Trivial Numeric Display", tnd );
}

