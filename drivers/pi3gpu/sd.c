/* Copyright (c) 2020 Simon Willcocks */

// Decisions to be made:
//  Expose the physical layer, or pick the "best" settings and expose only block device?
//    Depends on whether there are trade-offs between voltage and speed.
//  How to indicate when a command isn't supported by the card.
//  Simple interface, or paired objects, maybe with asynchronous calls?

#include "devices.h"

static uint32_t volatile command_thread = 0;
static uint32_t volatile data_thread = 0;
static uint32_t volatile interrupted = 0;

/*
 * class EXTERNAL_MULTI_MEDIA_CONTROLLER
 * feature
 *   card_inserted( new_card: SD_CARD
 * end
 */

uint32_t block_data[128] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
uint32_t block_index = 0;
uint32_t block_size = 0;

/*
 * Blocks: uint32_t *block; uint32_t *next_block;
 * Writer, at end of block, if (block != next_block) { block = next_block; }
 * Controller: can compare block, next_block, write next_block if equal
 */

// When a card is inserted, it will be reset, the highest speed options chosen, and enter whatever mode...? FIXME

//uint32_t SD__CMD8( j

extern void show_word( int x, int y, uint32_t number, uint32_t colour );

void emmc_interrupt()
{
  uint32_t new_interrupts = devices.emmc.INTERRUPT;

show_word( 300, 20, interrupted, 0xffff00ff );
show_word( 400, 20, new_interrupts, 0xffff00ff );
asm ( "svc 0" );

  if (new_interrupts != 0) {
    devices.emmc.INTERRUPT = new_interrupts; // Acknowledge interrupts.
    interrupted ++;
  }
  if (0 != (new_interrupts & 0x00000001)) {
    if (command_thread != 0)
      wake_thread( command_thread );
    // else update a counter, at least! FIXME
  }
  if (0 != (new_interrupts & 0x00000002)) {
    if (data_thread != 0)
      wake_thread( data_thread );
    // else update a counter, at least! FIXME
  }
  if (0 != (new_interrupts & 0x00000010)) {
    // Data write
    for (;;) asm ( "svc 1\nsvc 2\nsvc 5\nsvc 5" ); // Not doing that yet!
    if (data_thread != 0)
      wake_thread( data_thread );
    // else update a counter, at least! FIXME
  }
  if (0 != (new_interrupts & 0x00000020)) {
    // Data read
    while (block_size > 0) {
      block_data[block_index++] = devices.emmc.DATA;
      block_size -= 4;
    }
  }
  if (0 != (new_interrupts & ~0x00000033)) {
     asm ( "svc 1\nsvc 2\nsvc 3" );
  }
}

enum { SD_Card, UART0, UART1, USB_HCD, I2C0, I2C1, I2C2, SPI, CCP2TX, Unknown_RPi4_1, Unknown_RPi4_2 } device_ids;

static bool device_exists( uint32_t id )
{
  mailbox_request_buffer[0] = id;
  single_mailbox_tag_access( 0x00020001, 8 );
  return 0 == (mailbox_request_buffer[1] & 2);
}

static bool power_on_sd_host()
{
  if (!device_exists( SD_Card )) return false;
  if (0 == (mailbox_request_buffer[1] & 1)) {
    // Is off.
    mailbox_request_buffer[0] = SD_Card;
    mailbox_request_buffer[1] = 3; // Power on, and wait.
    single_mailbox_tag_access( 0x00028001, 8 );
    return 1 == mailbox_request_buffer[1];
  }
  return true;
}



enum { CLK_reserved, CLK_EMMC, CLK_UART, CLK_ARM,
       CLK_CORE, CLK_V3D, CLK_H264, CLK_ISP,
       CLK_SDRAM, CLK_PIXEL, CLK_PWM, CLK_HEVC,
       CLK_EMMC2, CLK_M2MC, CLK_PIXEL_BVB };

static uint32_t base_clock_rate( uint32_t id )
{
  mailbox_request_buffer[0] = id;
  single_mailbox_tag_access( 0x00030002, 8 );
  return mailbox_request_buffer[1];
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
  static uint32_t y = 20;
  show_word( 1400, y, cmd_code, 0xffffffff );
  show_word( 1480, y, arg, 0xffffffff );

  devices.emmc.ARG1 = arg;
  devices.emmc.CMDTM = cmd_code;
  dsb();

  wait_until_woken();
  response[0] = devices.emmc.RESP0;
  show_word( 1560, y, response[0], 0xffffffff );
  if (0x10000 == (cmd_code & 0x30000)) {
    response[1] = devices.emmc.RESP1;
    show_word( 1640, y, response[1], 0xffffffff );
    response[2] = devices.emmc.RESP2;
    show_word( 1720, y, response[2], 0xffffffff );
    response[3] = devices.emmc.RESP3;
    show_word( 1800, y, response[3], 0xffffffff );
  }
  asm ( "svc 0" );

  y += 20;
  if (y > 1000) y = 0;
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
  devices.interrupts.Enable_IRQs_2 = 0x40000000; // Arasan interrupt.
  dsb();

  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  if (!power_on_sd_host()) {
    for (;;) { asm volatile ( "svc 1\n\tsvc 7" ); }
  }

  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
  command_thread = this_thread;

  uint32_t control1 = devices.emmc.CONTROL1;
  devices.emmc.CONTROL0 = 0; 
  control1 |=  (1 << 24); // SRST_HC (Reset the complete host circuit)
  control1 &= ~(1 <<  2); // CLK_EN (SD clock enable)
  control1 &= ~(1 <<  0); // CLK_INTLEN (internal clocks enable)
  devices.emmc.CONTROL1 = control1;
  dsb();
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  while (0 != (devices.emmc.CONTROL1 & (1 << 24))) {
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
    yield();
  }
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  devices.emmc.CONTROL2 = 0;
  uint64_t base_clock_rate_hz = base_clock_rate( CLK_EMMC );
  // Pi 3: 250MHz

  if (base_clock_rate_hz < 400000) {
    for (;;) { asm volatile ( "svc 1\n\tsvc 8" ); }
  }
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

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
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  // All known interrupts, I like interrupts!
  // Not all known. According to circle/addon/SDCard/emmc.cpp, bits 6 and 7 are insertion and removal
  // On startup, bit 6 is set (card is inserted; level sensitive?)
  devices.emmc.IRPT_MASK = 0x017f71b7;
  devices.emmc.IRPT_EN   = 0x017f71b7;

  devices.emmc.SPI_INT_SPT   = 0xff; // Interrupt independent of card select line

  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
  sleep_ms( 2 ); // From circle driver, I don't know why, but it's quick!
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  control1 = devices.emmc.CONTROL1;
  control1 |= 0x00000004; // CLK_EN

  devices.emmc.CONTROL1 = control1;
  dsb();

  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
  sleep_ms( 2 ); // From circle driver, I don't know why, but it's quick!
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  while (0 == (devices.emmc.CONTROL1 & (1 << 1))) { // CLK_STABLE
    show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
    yield();
  }

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
    show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
    yield();
  }

  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
  command( 0, 0 );
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  command( 8, 0x1aa ); // Voltage supplied: 2.7-3.6V, recommended check pattern
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  // TODO Check for HC Check for reponse?
  // The important part of "48-bit" responses are returned in RESP0
  // The hardware checks the CRC, etc.
  if (0x1aa != (0xfff & response[0])) {
    for (;;) asm volatile ( "svc 1\nsvc 2\n svc 2" );
  }

  // Inquiry ACMD41:
  app_command( 41, idle, 0, 0 );
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  bool powered_up = false;
  while (!powered_up) {
    // Set HC
    app_command( 41, idle, 0, 0x40ff8000 );
    show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

    powered_up = (0 != (0x80000000 & response[0]));

    if (!powered_up) yield();
  }
  uint32_t ocr = (response[0] >> 8) & 0xffff;
  bool sdhc_supported = 0 != (response[0] & (1 << 30));

  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
  // Card should now be in ready mode
  // This is where we change the voltage, clock speed, etc., but I just want to read stuff... TODO
  command( 2, 0 );
  show_word( 1200, 800, response[0], 0xffff000f );
  show_word( 1200, 820, response[1], 0xffff000f );
  show_word( 1200, 840, response[2], 0xffff000f );
  show_word( 1200, 860, response[3], 0xffff000f );
  asm ( "svc 0" );
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
    show_word( 1200, 880, response[0], 0xfff0f00f );
    shifted_rca = response[0] & 0xffff0000;
    if (ident != ((response[0] >> 9) & 0xf)) {
      // Wasn't in ident when receiving the command?
      for (;;) asm volatile ( "svc 1\nsvc 2\n svc 4" );
    }
  }

  // Was in ident mode, now stby.
  command( 7, shifted_rca );
  show_word( 1200, 900, response[0], 0xfff00f0f );
  // Was in stby mode, now tran.

  app_command( 6, tran, shifted_rca, 2 );
  devices.emmc.CONTROL0 |= 2; // 4-bit bus (assumed, it's 2020!)

  // Try reading some data
  data_thread = this_thread;

  devices.emmc.BLKSIZECNT = (1 << 16) | 8; // 1 block of 8 bytes
  block_size = 8;

  app_command( 51, stby, shifted_rca, 0 );
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );
  wait_until_woken(); // As data_thread...
  show_word( 200, 20, __COUNTER__, 0xffffffff ); asm ( "svc 0" );

  show_word( 1300, 20, block_data[0], 0xffffffff ); asm ( "svc 0" );
  show_word( 1300, 40, block_data[1], 0xffffffff ); asm ( "svc 0" );
  //command( 17, 0 );

  show_word( 1200, 920, response[0], 0xfff00f0f );
}

typedef struct { 
  uint64_t lock; // Always first element in an exposed object.
  uint64_t count;
} emmc_service_object;

int emmc_service_handler( emmc_service_object *object, uint64_t call )
{
  object->count++;
  switch (call) {
  case 0x534400: // SD FIXME
    {
      initialise_sd_interface();
      return 0;
    }
    break;
  case 0x534401: // SD FIXME
    {
      initialise_sd_interface();
      return 0;
    }
    break;
  }
  return 1;
}

STACK_PER_OBJECT( emmc_service_object, 64 );

static struct emmc_service_object_container __attribute__(( aligned(16) )) emmc_service_singleton = { { 0 }, .object = { .lock = 0 } };

ISAMBARD_INTERFACE( BLOCK_DEVICE )
#include "interfaces/provider/BLOCK_DEVICE.h"
#include "interfaces/provider/SERVICE.h"

typedef union { integer_register r; emmc_service_object *p; } EMMC;

ISAMBARD_BLOCK_DEVICE__SERVER( EMMC )
ISAMBARD_SERVICE__SERVER( EMMC )
ISAMBARD_PROVIDER( EMMC, AS_BLOCK_DEVICE( EMMC ) ; AS_SERVICE( EMMC ) )
ISAMBARD_PROVIDER_UNLOCKED_PER_OBJECT_STACK( EMMC )

void expose_emmc()
{
  EMMC emmc = { .p = &emmc_service_singleton.object };
  SERVICE obj = EMMC_SERVICE_to_pass_to( system.r, emmc );
  register_service( "EMMC", obj );
}

