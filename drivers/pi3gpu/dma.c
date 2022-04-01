/* Copyright (c) 2021 Simon Willcocks */

#include "devices.h"

ISAMBARD_INTERFACE( TRIVIAL_NUMERIC_DISPLAY )

#include "interfaces/client/TRIVIAL_NUMERIC_DISPLAY.h"

extern TRIVIAL_NUMERIC_DISPLAY tnd;

#define N( n ) NUMBER__from_integer_register( n )

void expose_dma() {}
/*
{
  if (!current_device_bits_match()) return; // Should be calculated at compile time

  tnd = TRIVIAL_NUMERIC_DISPLAY__get_service( "Trivial Numeric Display", -1 );
  debug_progress = 0x11;
  //start_show_page_thread();
  debug_progress = 0x33;

  TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 900 ), N( 10 ), N( this_thread ), N( 0xfff0f0f0 ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 20 ), N( mapped_memory_pa ), N( 0xffffffff ) ); asm( "svc 0" );
  debug_progress = 1;

  for (int i = 0; i < 100; i++) { sleep_ms( 20 ); mapped_memory[2] = i; }

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 20 ), N( mapped_memory_pa ), N( 0xffffffff ) ); asm( "svc 0" );
  debug_progress = 3;
  if (!prepare_for_interrupts_from_sd_port()) return;

  debug_progress = 19;
  if (!initialise_sd_interface()) return;
  debug_progress = 20;

  EMMC__BLOCK_DEVICE__register_service( "DMA_MANAGER", &emmc_service_singleton );
}

*/
