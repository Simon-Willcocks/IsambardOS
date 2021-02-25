#include "devices.h"

#define MAX_REQUEST_SIZE 256 + 6

static uint32_t __attribute__(( aligned( 16 ) )) mailbox_request[MAX_REQUEST_SIZE] = { 0x20202020, 0x30303030, 0x40404040 };
uint32_t *const mailbox_request_buffer = &mailbox_request[5];

// Routines 

static void flush_and_invalidate_cache( void *start, int length )
{
  dsb();
  asm ( "svc 0" ); return;

  const uint32_t cache_line_size = 32; // Assumption FIXME
  uint8_t *p = start;
  while (length > 0) {
    asm volatile ( "dc ivac, %[va]" : : [va] "r" (p) );
    p += cache_line_size;
    length -= cache_line_size;
  }
}

static void perform_arm_vc_mailbox_transfer()
{
retry:
  flush_and_invalidate_cache( mailbox_request, mailbox_request[0] );
  static uint32_t request = 0;
  if (request == 0) {
    uint32_t phys_addr = physical_address_of( mailbox_request );
    request = 0x8 | phys_addr;
  }

  while (devices.mailbox[1].status & 0x80000000) { // Tx full
    yield();
  }

  // Channel 8: Request from ARM for response by VC
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

  flush_and_invalidate_cache( mailbox_request, mailbox_request[0] );
  yield();

  if ((mailbox_request[1] & 0x80000000) == 0) {
	  goto retry; // FIXME Why is this happening, especially since it doesn't when -fno-toplevel-reorder is there?
    for (;;) { asm volatile ( "svc 1\n\tsvc 5" ); }
  }
}

// Read/Write a single tag; buffer_size should be greater of request and response sizes
uint32_t single_mailbox_tag_access( uint32_t tag, uint32_t buffer_size )
{
  if (buffer_size > MAX_REQUEST_SIZE - 6) {
     for (;;) { asm volatile ( "svc 1\n\tsvc 6" ); }
  }

  mailbox_request[0] = buffer_size + 6 * sizeof( mailbox_request[0] );
  mailbox_request[1] = 0;
  mailbox_request[2] = tag;
  mailbox_request[3] = buffer_size;
  mailbox_request[4] = 0; // request
  mailbox_request[5 + (buffer_size / sizeof( mailbox_request[0] ))] = 0; // end tag

  perform_arm_vc_mailbox_transfer();

  return mailbox_request[4];
}

