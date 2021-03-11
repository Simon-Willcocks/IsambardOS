/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice and yield while waiting for mailbox response
// Doesn't play nice with anyone else using the mailbox

#include "drivers.h"

ISAMBARD_INTERFACE( FRAME_BUFFER )
#include "interfaces/client/FRAME_BUFFER.h"

unsigned long long stack_lock = 0;
unsigned long long __attribute__(( aligned( 16 ) )) stack[STACK_SIZE] = { 0x33333333 }; // Just a marker

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

void entry()
{
  SERVICE fb_server = { .r = 0 };

  do {
    fb_server = get_service( "Frame Buffer" );
    if (fb_server.r == 0) yield(); // FIXME add timeout to get_service
  } while (fb_server.r == 0);

  // ASSUMPTION FIXME
  FRAME_BUFFER fb = FRAME_BUFFER_from_integer_register( fb_server.r );

  PHYSICAL_MEMORY_BLOCK screen_page = FRAME_BUFFER__get_frame_buffer( fb );
  DRIVER_SYSTEM__map_at( driver_system(), screen_page, NUMBER_from_integer_register( mapped_address ) );

  uint64_t cache_line_size;
  asm volatile ( "mrs %[s], DCZID_EL0" : [s] "=r" (cache_line_size) );
  show_qword( 0, 1008, cache_line_size, 0xffffffff );
  show_qword( 160, 1008, 4 << (cache_line_size & 0xf), 0xffffffff );

  static uint64_t n = 0;
  for (n = 0;;n++) {
    show_qword( 0, 1024, n, Green );
    show_page( (void*) 0x1000 );
    asm ( "svc 0" );
    yield();
  }
}

