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
  static uint32_t __attribute__(( aligned( 16 ) )) mailbox_request[33] = {
#else
  static uint32_t __attribute__(( aligned( 16 ) )) mailbox_request[26] = {
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

  mailbox_tag_request( mailbox_request );

  physical_address = (mailbox_request[5] & 0x3fffffff);
  memory_size = mailbox_request[6];

  uint32_t fake_size = (memory_size + (2 << 20)-1) & ~((2ull << 20)-1);
  // Size made to multiple of 2M. Not true, but quick to implement! FIXME when find_and_map_memory is more refined
  screen_page = DRIVER_SYSTEM__get_physical_memory_block( driver_system(), NUMBER__from_integer_register( physical_address ), NUMBER__from_integer_register( fake_size ) );
}

void map_screen()
{
  if (!screen_mapped) {
    while (screen_page.r == 0) { initialise_display(); }

    DRIVER_SYSTEM__map_at( driver_system(), screen_page, NUMBER__from_integer_register( (uint64_t) mapped_address ) );
    screen_mapped = true;
  }
}

typedef struct { 
  uint64_t count;
} fb_service_object;

static fb_service_object fb_service_singleton = { .count = 0 };

ISAMBARD_INTERFACE( FRAME_BUFFER )

#include "interfaces/provider/FRAME_BUFFER.h"

typedef fb_service_object *FB;
ISAMBARD_STACK( fb_stack, 96 );
uint64_t fb_lock = 0;

ISAMBARD_FRAME_BUFFER__SERVER( FB )
ISAMBARD_PROVIDER( FB, AS_FRAME_BUFFER( FB ) )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( FB, RETURN_FUNCTIONS_FRAME_BUFFER( FB ), fb_lock, fb_stack, (96 * 8) )

void expose_frame_buffer()
{
  FB__FRAME_BUFFER__register_service( "Frame Buffer", &fb_service_singleton );
}

void FB__FRAME_BUFFER__get_frame_buffer( FB o )
{
  o = o;

  while (screen_page.r == 0) { initialise_display(); }

  FB__FRAME_BUFFER__get_frame_buffer__return( PHYSICAL_MEMORY_BLOCK__duplicate_to_return( screen_page ) );
}
