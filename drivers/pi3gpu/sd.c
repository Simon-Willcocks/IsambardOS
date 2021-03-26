/* Copyright (c) 2020 Simon Willcocks */

// Decisions to be made:
//  Expose the physical layer, or pick the "best" settings and expose only block device?
//    Depends on whether there are trade-offs between voltage and speed.
//  How to indicate when a command isn't supported by the card.
//  Simple interface, or paired objects, maybe with asynchronous calls?

#include "devices.h"

#define N( n ) NUMBER__from_integer_register( n )

static uint32_t volatile command_thread = 0;
static uint32_t volatile data_thread = 0;
static uint32_t volatile interrupted = 0;

/*
 * class EXTERNAL_MULTI_MEDIA_CONTROLLER
 * feature
 *   card_inserted( new_card: SD_CARD
 * end
 */

uint32_t *block_data = 0;
uint32_t block_index = 0;
uint32_t block_size = 0;

uint32_t debug_progress = 0;
uint32_t debug_last_command = 0;
uint32_t debug_last_acommand = 0;
uint32_t debug_last_interrupts = 0;
uint32_t debug_all_interrupts = 0;
uint32_t debug_interrupted = 0;

/*
 * Blocks: uint32_t *block; uint32_t *next_block;
 * Writer, at end of block, if (block != next_block) { block = next_block; }
 * Controller: can compare block, next_block, write next_block if equal
 */

// When a card is inserted, it will be reset, the highest speed options chosen, and enter whatever mode...? FIXME

void emmc_interrupt()
{
  uint32_t new_interrupts = devices.emmc.INTERRUPT;

  memory_read_barrier(); // Completed our reads of devices.emmc

  if (new_interrupts != 0) {
    debug_last_interrupts = new_interrupts;
    debug_all_interrupts |= new_interrupts;
    debug_interrupted ++;

    memory_write_barrier(); // About to write to devices.emmc
    devices.emmc.INTERRUPT = new_interrupts; // Acknowledge interrupts.

    if (0 != (new_interrupts & 0x00000001)) {
      if (command_thread != 0)
        wake_thread( command_thread );
      else debug_interrupted += 0x10;
    }
    if (0 != (new_interrupts & 0x00000002)) {
      if (data_thread != 0)
        wake_thread( data_thread );
      else debug_interrupted += 0x100;
    }
    if (0 != (new_interrupts & 0x00000010)) {
      // Data write
      for (;;) asm ( "svc 1\nsvc 2\nsvc 5\nsvc 5" ); // Not doing that yet!
      if (data_thread != 0)
        wake_thread( data_thread );
      else debug_interrupted += 0x1000;
    }
    if (0 != (new_interrupts & 0x00000020)) {
      // Data read
      while (block_size > 0) {
        block_data[block_index++] = devices.emmc.DATA;
        block_size -= 4;
      }
      memory_read_barrier(); // Completed our reads of devices.emmc
    }
  }

  if (0 != (new_interrupts & ~0x00000033)) {
     // asm ( "svc 1\nsvc 2\nsvc 3" );
  }
}

enum device_id { SD_Card, UART0, UART1, USB_HCD, I2C0, I2C1, I2C2, SPI, CCP2TX, Unknown_RPi4_1, Unknown_RPi4_2 };
enum device_power_state { POWER_STATE_OFF = 0, POWER_STATE_ON = 1, DOES_NOT_EXIST = 2, ON_BUT_DOES_NOT_EXIST = 3 };

static enum device_power_state power_state( enum device_id id )
{
  uint32_t __attribute(( aligned( 16 ) )) request[8] = { sizeof( request ), 0, 0x00020001, 8, 0, id, 0, 0 };
  mailbox_tag_request( request );

  if (request[1] != 0x80000000 || request[4] != 0x80000008) {
    return false;
  }
  return request[6];
}

static bool power_up( enum device_id id )
{
  enum device_power_state state = power_state( id );

  if (state == POWER_STATE_OFF) {
    uint32_t __attribute(( aligned( 16 ) )) power_on_request[13] =
        { sizeof( power_on_request ), 0,
		0x00028001, 8, 0, id, 1,           // Power up
		0x00020002, 8, 0, id, 0,           // How long will it take?
                0 };
    // Power on, no wait (we'll sleep).
    mailbox_tag_request( power_on_request );

    if (power_on_request[1] != 0x80000000 || power_on_request[4] != 0x80000008
     || power_on_request[9] != 0x80000000 || power_on_request[10] != 0x80000008
     || power_on_request[11] != id) {
      return false;
    }

    sleep_ms( (power_on_request[11] + 999)/1000 ); // Microseconds

    // Is on?
    state = power_state( id );

    return state == POWER_STATE_ON;
  }
  return true;
}

enum clock { CLK_reserved, CLK_EMMC, CLK_UART, CLK_ARM,
             CLK_CORE, CLK_V3D, CLK_H264, CLK_ISP,
             CLK_SDRAM, CLK_PIXEL, CLK_PWM, CLK_HEVC,
             CLK_EMMC2, CLK_M2MC, CLK_PIXEL_BVB };

static uint32_t base_clock_rate( enum clock clock_id )
{
  uint32_t __attribute(( aligned( 16 ) )) request[8] = { sizeof( request ), 0, 0x00030002, 8, 0, clock_id, 0, 0 };
  mailbox_tag_request( request );

  if (request[1] != 0x80000000 || request[4] != 0x80000008) {
    return 0;
  }
  return request[6];
}

static uint32_t const cmds[] = { 
  [ 0] 0x00000000,
  [ 2] 0x02090000, // no data transfer, no index check, check response crc, 136-bit response, no multi-block
  [ 3] 0x030a0000, // no data transfer, no index check, check response crc, 48-bit response, no multi-block
  [ 7] 0x070b0000, // no data transfer, no index check, check response crc, 48-bit response, no multi-block
  [ 8] 0x080a0000, // no data transfer, no index check, check response crc, 48-bit response without busy, no multi-block
  [17] 0x112a0010, // data read, no index check, check response crc, 48-bit response without busy, no multi-block
  [55] 0x370a0000  // no data transfer, no index check, check response crc, 48-bit response without busy, no multi-block
};

// Known app commands, must be preceded by a cmd[55]
static uint32_t const acmds[] = {
  [ 6] 0x06020000, // no data transfer, no index check, no crc check, 48-bit response without busy, no multi-block
  [41] 0x29020000, // no data transfer, no index check, no crc check, 48-bit response without busy, no multi-block
  [51] 0x33220010  // data read, no index check, no crc check, 48-bit response without busy, no multi-block
};

static uint32_t response[4];

typedef enum { idle, ready, ident, stby, tran, data, rcv, prg, dis, ina } sd_states;


static inline void cmdtm( int cmd_code, uint32_t arg )
{
static int commands = 0x100;
debug_last_command = cmd_code + ((commands++) << 12);

  devices.emmc.ARG1 = arg;
  devices.emmc.CMDTM = cmd_code;
  dsb();

  wait_until_woken();
  response[0] = devices.emmc.RESP0;
  if (0x10000 == (cmd_code & 0x30000)) {
    response[1] = devices.emmc.RESP1;
    response[2] = devices.emmc.RESP2;
    response[3] = devices.emmc.RESP3;
  }
}

static inline void command( int cmd, uint32_t arg )
{
  if (cmd != 0 && cmds[cmd] == 0) {
    asm ( "svc 1\nsvc 2\nsvc 2" );
  }
  cmdtm( cmds[cmd], arg );
}

static inline void app_command( int acmd, sd_states expected_state, uint32_t shifted_rca, uint32_t arg )
{
debug_last_acommand = acmd;

  if (acmds[acmd] == 0) {
    asm ( "svc 1\nsvc 2\nsvc 2" );
  }
  command( 55, shifted_rca );
  if (((response[0] >> 9) & 0xf) != expected_state) {
    asm ( "svc 1\nsvc 2\nsvc 5" );
  }
  cmdtm( acmds[acmd], arg );
}

#define MEMSET_WITHOUT_ASM
#define assert( x ) if (x) {}
#include "memset.h"

void initialise_sd_interface()
{
  debug_progress = 3;

  memory_write_barrier(); // About to write to devices.interrupts
  devices.interrupts.Enable_IRQs_2 = 0x40000000; // Arasan interrupt.
  dsb();

  if (!power_up( SD_Card )) {
    debug_progress = 0xf1; wait_until_woken();
    for (;;) { asm volatile ( "brk 2" ); }
  }
  debug_progress = 8;

  memory_write_barrier(); // About to write to devices.emmc

  command_thread = this_thread;

  uint32_t control1 = devices.emmc.CONTROL1;
  devices.emmc.CONTROL0 = 0; 
  control1 |=  (1 << 24); // SRST_HC (Reset the complete host circuit)
  control1 &= ~(1 <<  2); // CLK_EN (SD clock enable)
  control1 &= ~(1 <<  0); // CLK_INTLEN (internal clocks enable)
  devices.emmc.CONTROL1 = control1;

  memory_read_barrier(); // Completed our reads of devices.emmc
  debug_progress = 9; yield();

  while (0 != (devices.emmc.CONTROL1 & (1 << 24))) {
    memory_read_barrier(); // Completed our reads of devices.emmc
    yield();
  }

  memory_read_barrier(); // Completed our reads of devices.emmc
  debug_progress = 10; yield();
  memory_write_barrier(); // About to write to devices.emmc

  devices.emmc.CONTROL2 = 0;
  uint64_t base_clock_rate_hz = base_clock_rate( CLK_EMMC );
  // Pi 3: 250MHz

  if (base_clock_rate_hz < 400000) {
    for (;;) { asm volatile ( "svc 1\n\tsvc 8" ); }
  }

  // NOTE: This code is assuming a fixed clock + divider, but the SoC seems
  // to allow programmable clocks; min, max, set, etc. via mailbox i/f.
  // Might be better the other way? (I'm assuming that's what bit 5 means.)

  // set to 400KHz, for identification phase
  uint32_t target_rate = 400000;
  uint64_t divisor = (base_clock_rate_hz + target_rate - 1)/ target_rate;

  control1 = devices.emmc.CONTROL1;
  control1 |= 0x00000001; // CLK_INTLEN (Clock enable for internal EMMC clocks for power saving)
  control1 &= 0xfff0ffdf; // Clears CLK_GENSEL, DATA_TOUNIT
  control1 |= 0x000e0000; // Maximum timeout
  control1 &= 0xffff003f; // Clears CLK_FREQ8, CLK_FREQ_MS2
  control1 |= ((divisor & 0xff) << 8) | ((divisor & 0x300) >> 2);
  devices.emmc.CONTROL1 = control1;
  memory_read_barrier(); // Completed our reads of devices.emmc

  memory_read_barrier(); // Completed our reads of devices.emmc
  debug_progress = 11; yield();
  sleep_ms( 2 ); // From circle driver, I don't know why, but it's quick!

  memory_write_barrier(); // About to write to devices.emmc
  control1 = devices.emmc.CONTROL1;
  control1 |= 0x00000004; // CLK_EN

  devices.emmc.CONTROL1 = control1;
  dsb();
  memory_read_barrier(); // Completed our reads of devices.emmc

  sleep_ms( 2 ); // From circle driver, I don't know why, but it's quick!

  // All known interrupts, I like interrupts!
  // Not all known. According to circle/addon/SDCard/emmc.cpp, bits 6 and 7 are insertion and removal
  // On startup, bit 6 is set (card is inserted; level sensitive?)
  devices.emmc.IRPT_MASK = 0x017f71b7;
  devices.emmc.IRPT_EN   = 0x017f71b7;

  devices.emmc.SPI_INT_SPT   = 0xff; // Interrupt independent of card select line

  while (0 == (devices.emmc.CONTROL1 & (1 << 1))) { // CLK_STABLE
    memory_read_barrier(); // Completed our reads of devices.emmc
    yield();
  }

  debug_progress = 12; yield();
  // idle, ready, ident, stby, tran, data, rcv, prg, dis, ina
  // State diagram for the initial states is trivial:
  // CMD0 -> idle -> [ CMD8 -> idle ] -> ACMD41 -> ready -> CMD2 -> ident -> CMD3 -> stby
  //   CMD2: All cards, send CID
  //   CMD3: Card, send new relative address (request is not addressed, I don't know how multiple cards work)
  //
  // stby -> CMD[3,4,7(0),9,10,13,55] -> stby
  //   CMD9: (address) get card specific data
  //   CMD4: set bus layout and frequency (optional)
  //   CMD7: (with address 0) puts all cards in stby
  //   CMD10: (address) send card identification (CID)
  // stby -> CMD7 (card is addressed) -> tran
  //   CMD7: (address <> 0) selects card for transfers
  // stby -> CMD15 -> ina

  // First command... Go idle
  while (devices.emmc.STATUS & 3) {
    memory_read_barrier(); // Completed our reads of devices.emmc
debug_last_acommand ++;
    yield();
  }
  debug_progress = 13; yield();

  command( 0, 0 );

  command( 8, 0x1aa ); // Voltage supplied: 2.7-3.6V, recommended check pattern

  // TODO Check for HC Check for reponse?
  // The important part of "48-bit" responses are returned in RESP0
  // The hardware checks the CRC, etc.
  if (0x1aa != (0xfff & response[0])) {
    for (;;) asm volatile ( "svc 1\nsvc 2\n svc 2" );
  }

  debug_progress = 16; yield();
  // Inquiry ACMD41:
  app_command( 41, idle, 0, 0 );

  bool powered_up = false;
  while (!powered_up) {
    // Set HC
    app_command( 41, idle, 0, 0x40ff8000 );

    powered_up = (0 != (0x80000000 & response[0]));

    if (!powered_up) yield();
  }
  //uint32_t ocr = (response[0] >> 8) & 0xffff;
  //bool sdhc_supported = 0 != (response[0] & (1 << 30));

  // Card should now be in ready mode
  // This is where we change the voltage, clock speed, etc., but I just want to read stuff... TODO
  command( 2, 0 );
  /* Name                       Field   Width   CID-slice
     Manufacturer ID            MID     8       [127:120]
     OEM/Application ID         OID     16      [119:104]
     Product name               PNM     40      [103:64]
     Product revision           PRV     8       [63:56]
     Product serial number      PSN     32      [55:24]
     reserved                   -       4       [23:20]
     Manufacturing date         MDT     12      [19:8]
     CRC7 checksum              CRC     7       [7:1]
     not used, always 1         -       1       [0:0]

     Example RESP0, 1, 2, 3:
     000300a2 44001280 41505053 00003000 
     None are odd, which is odd. Guess the CRC7 and always 1 bits are not included.
     44, 41, 50, 50, 53 are all ASCII: D A P P S...
  */

  // Card should now be in ident mode
  uint32_t shifted_rca = 0;
  // This loops, in case the card returns a zero address, which is invalid, so we'll ask for another...
  while (shifted_rca == 0) {
    command( 3, 0 );
    shifted_rca = response[0] & 0xffff0000;
    if (ident != ((response[0] >> 9) & 0xf)) {
      // Wasn't in ident when receiving the command?
      for (;;) asm volatile ( "svc 1\nsvc 2\n svc 4" );
    }
  }

  debug_progress = 17; yield();

  // Was in ident mode, now stby.
  command( 7, shifted_rca );
  // Was in stby mode, now tran.

  app_command( 6, tran, shifted_rca, 2 );
  devices.emmc.CONTROL0 |= 2; // 4-bit bus (assumed, it's 2020!)

  // Try reading some data
  data_thread = this_thread;

  devices.emmc.BLKSIZECNT = (1 << 16) | 8; // 1 block of 8 bytes
  block_index = 0;
  block_size = 8;
  block_data = (void*) 0x10100;

  debug_progress = 18; yield();

  app_command( 51, stby, shifted_rca, 0 );
  wait_until_woken(); // As data_thread...

  debug_progress = 19; yield();

  memory_write_barrier(); // About to write to devices.emmc
  devices.emmc.BLKSIZECNT = (1 << 16) | 512; // 1 block of 512 bytes
  block_index = 0;
  block_size = 512;
  block_data = (void*) 0x10200;
  command( 17, 0xc2c0 ); // testblock.bin
  wait_until_woken(); // As data_thread...

  flush_and_invalidate_cache( (void*) 0x10200, 512 );
}

typedef struct EMMC { 
  uint64_t count;
} *EMMC;

static struct EMMC emmc_service_singleton = { 0 };
uint64_t __attribute__(( aligned(16) )) emmc_stack[64];
uint64_t emmc_lock = 0;

ISAMBARD_INTERFACE( BLOCK_DEVICE )
#include "interfaces/provider/BLOCK_DEVICE.h"

ISAMBARD_INTERFACE( TRIVIAL_NUMERIC_DISPLAY )

#include "interfaces/client/TRIVIAL_NUMERIC_DISPLAY.h"

ISAMBARD_BLOCK_DEVICE__SERVER( EMMC )
ISAMBARD_PROVIDER( EMMC, AS_BLOCK_DEVICE( EMMC ) )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( EMMC, RETURN_FUNCTIONS_BLOCK_DEVICE( EMMC ), emmc_lock, emmc_stack, 64 * 8 )

PHYSICAL_MEMORY_BLOCK test_memory;
uint32_t *mapped_memory = (void*) (0x10000);

TRIVIAL_NUMERIC_DISPLAY tnd = {};
uint32_t initialisation_thread = -1;

void show_page_thread()
{
  test_memory = SYSTEM__allocate_memory( system, NUMBER__from_integer_register( 4096 ) );
  DRIVER_SYSTEM__map_at( driver_system(), test_memory, NUMBER__from_pointer( mapped_memory ) );

  mapped_memory[0] = 0;
  mapped_memory[1] = 0;

  tnd = TRIVIAL_NUMERIC_DISPLAY__get_service( "Trivial Numeric Display", -1 );

  wake_thread( initialisation_thread );

  TRIVIAL_NUMERIC_DISPLAY__set_page_to_show( tnd, test_memory, NUMBER__from_pointer( mapped_memory ) );
  for (;;) {
    mapped_memory[1] ++;
    yield();
    TRIVIAL_NUMERIC_DISPLAY__show_page( tnd );
    TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( 10 ), N( 10 ), N( debug_progress ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 32 ), N( 10 ), N( debug_last_command ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 132 ), N( 10 ), N( debug_last_acommand ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 232 ), N( 10 ), N( debug_last_interrupts ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 332 ), N( 10 ), N( debug_all_interrupts ), N( 0xffffffff ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 432 ), N( 10 ), N( debug_interrupted ), N( 0xfff0f0f0 ) );
  }
}

void start_show_page_thread()
{
  static struct {
    uint64_t stack[64];
  } __attribute__(( aligned(16) )) stack = {};

  create_thread( show_page_thread, (uint64_t*) ((&stack)+1) );
}

void expose_emmc()
{
  initialisation_thread = this_thread;

  start_show_page_thread();

  wait_until_woken(); // While debugging, this means that the frame buffer had been initialised, so we're the only driver using the mailbox

  debug_progress = 1;

  EMMC__BLOCK_DEVICE__register_service( "EMMC", &emmc_service_singleton );

  debug_progress = 2;
  for (int i = 0; i < 100; i++) { sleep_ms( 20 ); mapped_memory[2] = i; }

  initialise_sd_interface();
  debug_progress = 20;

  wait_until_woken();
}

void EMMC__BLOCK_DEVICE__read_4k_pages( EMMC o, PHYSICAL_MEMORY_BLOCK dest, NUMBER start, NUMBER count )
{
  o = o; dest = dest; start = start; count = count;
  EMMC__BLOCK_DEVICE__read_4k_pages__return();
}

