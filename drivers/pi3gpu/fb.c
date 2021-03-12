/* Copyright (c) 2020 Simon Willcocks */

// Provide access to the frame buffer, GPU timers, and GPIO.
//
// Current limitations:.
// Only does Full HD 1920x1080
// Only does 32-bit colour depth
// Doesn't play nice with anyone else using the mailbox

#include "devices.h"


// Suitable for when caches enabled (128x faster?)
#define LED_BLINK_TIME 0x8000000

static inline void led_off( )
{
  memory_write_barrier(); // About to write to devices.gpio
  devices.gpio.GPCLR[0] = (1 << 4);
}

static inline void led_on( )
{
  memory_write_barrier(); // About to write to devices.gpio
  devices.gpio.GPSET[0] = (1 << 4);
}

void led_blink( int n ) {
  // Count the blinks! Extra short = 0, Long = 5

  if (n == 0) {
    led_on();
    for (uint64_t i = 0; i < LED_BLINK_TIME / 4; i++) { asm volatile ( "" ); }
    led_off();
    for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
  }
  else {
    while (n >= 5) {
      led_on();
      for (uint64_t i = 0; i < LED_BLINK_TIME * 4; i++) { asm volatile ( "" ); }
      led_off();
      for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
      n -= 5;
    }
    while (n > 0) {
      led_on();
      for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
      led_off();
      for (uint64_t i = 0; i < LED_BLINK_TIME; i++) { asm volatile ( "" ); }
      n --;
    }
  }
  for (uint64_t i = 0; i < 4 * LED_BLINK_TIME; i++) { asm volatile ( "" ); }
}

void blink_number( uint32_t number )
{
  for (int i = 28; i >= 0; i -= 4) {
    led_blink( (number >> i) & 0xf );
  }
}

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

PHYSICAL_MEMORY_BLOCK screen_page = { .r = 0 };
bool screen_mapped = false;

void map_screen();

// FIXME Only 1080p at the moment.
// Overscan is affected by config.txt but turning it off results in the display not fitting on the screen.
// FIXME Combine with tag read/writes, including blocking
void initialise_display()
{
#define overscan 24
#ifdef overscan
  static uint32_t __attribute__(( aligned( 16 ) )) volatile mailbox_request[33] = {
#else
  static uint32_t __attribute__(( aligned( 16 ) )) volatile mailbox_request[26] = {
#endif
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
#ifdef overscan
          0x0004800a, // Set overscan; this seems to depend on the monitor
          16, 0,   overscan, overscan, overscan, overscan,
#endif
          0 }; // End of tags tag

  NUMBER mailbox_request_PA = DRIVER_SYSTEM__physical_address_of( driver_system(),
                NUMBER_from_pointer( (void*) &mailbox_request ) );

  NUMBER response;

  // If the content of the structure is no longer fixed, the following line will be needed:
  // flush_and_invalidate_cache( mailbox_request, mailbox_request[0] );

  response = GPU_MAILBOX_CHANNEL__send_for_response( channel8, mailbox_request_PA );

  flush_and_invalidate_cache( mailbox_request, mailbox_request[0] );

  if (response.r != mailbox_request_PA.r) {
     for (;;) { led_blink( 2 ); }
  }

  if ((mailbox_request[1] & 0x80000000) == 0) {
    for (;;) { led_blink( 3 ); }
  }

  physical_address = (mailbox_request[5] & 0x3fffffff);
  memory_size = mailbox_request[6];

  uint32_t fake_size = (memory_size + (2 << 20)-1) & ~((2ull << 20)-1);
  // Size made to multiple of 2M. Not true, but quick to implement! FIXME when find_and_map_memory is more refined
  screen_page = DRIVER_SYSTEM__get_physical_memory_block( driver_system(), NUMBER_from_integer_register( physical_address ), NUMBER_from_integer_register( fake_size ) );
}

void map_screen()
{
  if (!screen_mapped) {
    while (screen_page.r == 0) { initialise_display(); }

    DRIVER_SYSTEM__map_at( driver_system(), screen_page, NUMBER_from_integer_register( (uint64_t) mapped_address ) );
    screen_mapped = true;
  }
}

typedef struct { 
  uint64_t lock; // Always first element in an exposed object.
  uint64_t count;
} fb_service_object;

STACK_PER_OBJECT( fb_service_object, 64 );

static struct fb_service_object_container __attribute__(( aligned(16) )) fb_service_singleton = { { 0 }, .object = { .lock = 0 } };

ISAMBARD_INTERFACE( FRAME_BUFFER )

#include "interfaces/provider/FRAME_BUFFER.h"
#include "interfaces/provider/SERVICE.h"

typedef union { integer_register r; fb_service_object *p; } FB;

ISAMBARD_FRAME_BUFFER__SERVER( FB )
ISAMBARD_SERVICE__SERVER( FB )
ISAMBARD_PROVIDER( FB, AS_FRAME_BUFFER( FB ) ; AS_SERVICE( FB ) )
ISAMBARD_PROVIDER_UNLOCKED_PER_OBJECT_STACK( FB )

void expose_frame_buffer()
{
  FB fb = { .p = &fb_service_singleton.object };
  SERVICE obj = FB_SERVICE_to_pass_to( system.r, fb );
  register_service( "Frame Buffer", obj );
}

PHYSICAL_MEMORY_BLOCK FB__FRAME_BUFFER__get_frame_buffer( FB o )
{
  o = o;
  map_screen();
  return PHYSICAL_MEMORY_BLOCK_duplicate_to_return( screen_page );
}
