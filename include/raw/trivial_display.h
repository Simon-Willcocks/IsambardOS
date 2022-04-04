/* Copyright (c) 2021 Simon Willcocks */

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
      show_word( x * 68 + 72, y * 8 + 64, number[x + y * 16], (number[x + y * 16]) ? colour : colour & ~0x00cccccc );
    }
  }
}

