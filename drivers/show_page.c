/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice and yield while waiting for mailbox response
// Doesn't play nice with anyone else using the mailbox

#include "drivers.h"

DRIVER_INITIALISER( entry );

unsigned long long stack_lock = 0;
unsigned long long system = 0; // Initialised by _start code
unsigned long long __attribute__(( aligned( 16 ) )) stack[STACK_SIZE] = { 0x33333333 }; // Just a marker

static const uint32_t width = 1920;
static const uint32_t height = 1080;
static const uint32_t vwidth = 1920;
static const uint32_t vheight = 1080;
static uint32_t *const mapped_address = (void*) (2 << 20);
static uint32_t memory_size = width * height * 4;

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
  Object fb_server = 0;

  do {
    fb_server = get_service( 0xfb ); // FIXME crc32 stuff, when services are working...
    if (fb_server == 0) yield(); // FIXME add timeout to get_service
  } while (fb_server == 0);

  Object screen_page = inter_map_call_0p( fb_server, 0xfb );
  map_physical_block_at( screen_page, (uint64_t) mapped_address );

  uint64_t cache_line_size;
  asm volatile ( "mrs %[s], DCZID_EL0" : [s] "=r" (cache_line_size) );
  show_qword( 0, 1008, cache_line_size, 0xffffffff );
  show_qword( 160, 1008, 4 << (cache_line_size & 0xf), 0xffffffff );

  inter_map_call_0p( fb_server, 0x5344 );

  for (uint64_t n = 0;;n++) {
    show_qword( 0, 1024, n, 0xffffffff );
    asm ( "svc 0" );
    // The problem doesn't seem to be the changing map to system
    //inter_map_procedure_0p( system, 99 ); // Blink, and don't change map on interrupt
    inter_map_procedure_0p( fb_server, 4 ); // Show some debug information
    yield();
  }
}

