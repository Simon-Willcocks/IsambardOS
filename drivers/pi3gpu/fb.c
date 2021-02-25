/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer, GPU timers, and GPIO.
//
// Current limitations:.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice with anyone else using the mailbox

#include "devices.h"

static uint64_t physical_address;
static uint32_t *const mapped_address = (void*) (2 << 20);
static uint32_t memory_size;
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

Object screen_page = 0;
bool screen_mapped = false;

void map_screen();

// FIXME Only 1080p at the moment.
// FIXME Combine with tag read/writes, including blocking
void initialise_display()
{
  static uint32_t __attribute__(( aligned( 16 ) )) mailbox_request[33] = {
          sizeof( mailbox_request ), 0, // Message buffer size, request
          // Tags: Tag, buffer size, request code, buffer
          0x00040001, // Allocate buffer
          8, 0,    2 << 20, 0, // Size, Code, In: Alignment, Out: Base, Size
          0x00048003, // Set physical (display) width/height
          8, 0,    width, height,
          0x00048004, // Set virtual (buffer) width/height
          8, 0,    vwidth, vheight,
          0x00048005, // Set depth
          4, 0,    32,
          0x00048006, // Set pixel order
          4, 0,    0,    // 0 = BGR, 1 = RGB
          0x0004800a, // Set overscan
          16, 0,   0, 0, 0, 0,    // All zeros
          0 }; // End of tags tag

  uint64_t mailbox_request_physical_address = physical_address_of( mailbox_request );

  while (devices.mailbox[1].status & 0x80000000) { // Tx full
    yield();
  }

  // Channel 8: Request from ARM for response by VC
  uint32_t request = 0x8 | (uint32_t) mailbox_request_physical_address;
  devices.mailbox[1].value = request;
  dsb();
  uint32_t response;

  while (devices.mailbox[0].status & 0x40000000) { // Rx empty
    yield();
  }

  response = devices.mailbox[0].value;

  if (response != request) {
     for (;;) { asm volatile ( "svc 1\n\tsvc 4" ); }
  }

  if ((mailbox_request[1] & 0x80000000) == 0) {
    for (;;) { asm volatile ( "svc 1\n\tsvc 6" ); }
  }

  physical_address = (mailbox_request[5] & 0x3fffffff);
  memory_size = mailbox_request[6];

  uint32_t fake_size = (memory_size + (2 << 20)-1) & ~((2ull << 20)-1);
  // Size made to multiple of 2M. Not true, but quick to implement! FIXME when find_and_map_memory is more refined
  screen_page = get_physical_memory_block( physical_address, fake_size );


  map_screen();
  uint32_t *p = mapped_address;
  for (int i = 0; i < 1024 * 1024; i++) {
    p[i] = 0xffff3388;
  }
}

void map_screen()
{
  if (!screen_mapped) {
    while (screen_page == 0) { initialise_display(); }

    map_physical_block_at( screen_page, (uint64_t) mapped_address );
    screen_mapped = true;
  }
}

typedef struct { 
  uint64_t lock; // Always first element in an exposed object.
  uint64_t count;
} fb_service_object;

int fb_service_handler( fb_service_object *object, uint64_t call )
{
  object->count++;
  switch (call) {
  case 0xfb: // FIXME
    while (screen_page == 0) { initialise_display(); }

    Object dup = duplicate_to_return( screen_page );

    return dup;

  case 0x5344: // SD FIXME Not really for this object, but so show_page can execute it
    {
      map_screen();
      extern void initialise_sd_interface();
      show_word( 100, 20, 0x12341234, White );
      initialise_sd_interface();
      return 0;
    }
    break;

  default:
    if (!screen_mapped) {
      while (screen_page == 0) { initialise_display(); }

      map_physical_block_at( screen_page, (uint64_t) mapped_address );
      screen_mapped = true;
    }

    show_word( 1700, 16, call, Red );
    show_word( 1800, 16, object->count, Red );
    break;
  }
  return 1;
}

STACK_PER_OBJECT( fb_service_object, 64 );
SIMPLE_CALL_VENEER( fb_service );

static struct fb_service_object_container __attribute__(( aligned(16) )) fb_service_singleton = { { 0 }, .object = { .lock = 0 } };

void expose_frame_buffer()
{
  Object service = object_to_pass_to( system, fb_service_veneer, (uint64_t) &fb_service_singleton.object.lock );

  register_service( 0xfb, service ); // FIXME crc32
}
