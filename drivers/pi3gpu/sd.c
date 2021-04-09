/* Copyright (c) 2020 Simon Willcocks */

// Decisions to be made:
//  Expose the physical layer, or pick the "best" settings and expose only block device?
//    Depends on whether there are trade-offs between voltage and speed.
//  How to indicate when a command isn't supported by the card.
//  Simple interface, or paired objects, maybe with asynchronous calls?

// Protocol documents from https://www.sdcard.org/downloads/pls/
// Initially, only supporting modern SD cards.

#include "devices.h"

ISAMBARD_INTERFACE( TRIVIAL_NUMERIC_DISPLAY )

#include "interfaces/client/TRIVIAL_NUMERIC_DISPLAY.h"

TRIVIAL_NUMERIC_DISPLAY tnd = {};
extern uint32_t *mapped_memory;
extern uint32_t mapped_memory_pa;

#define N( n ) NUMBER__from_integer_register( n )

typedef enum { idle, ready, ident, stby, tran, data, rcv, prg, dis, ina } sd_states;

static uint32_t volatile command_thread = 0;
static uint32_t volatile current_cmdtm = 0;
static uint32_t volatile pending_app_cmdtm = 0;
static uint32_t volatile pending_app_command_arg = 0;
static sd_states volatile acmd_expected_state = idle;

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

#define debug_progress mapped_memory[48]
#define debug_last_command mapped_memory[49]
#define debug_last_acommand mapped_memory[50]
#define debug_last_interrupts mapped_memory[51]
#define debug_all_interrupts mapped_memory[52]
#define debug_interrupted mapped_memory[53]

/*
 * Blocks: uint32_t *block; uint32_t *next_block;
 * Writer, at end of block, if (block != next_block) { block = next_block; }
 * Controller: can compare block, next_block, write next_block if equal
 */

// When a card is inserted, it will be reset, the highest speed options chosen, and enter whatever mode...? FIXME

static uint32_t const cmds[] = { 
  [ 0] 0x00000000,
  [ 2] 0x02090000, // no data transfer, no index check, check response crc, 136-bit response, no multi-block
  [ 3] 0x030a0000, // no data transfer, no index check, check response crc, 48-bit response, no multi-block
  [ 7] 0x070b0000, // no data transfer, no index check, check response crc, 48-bit response, no multi-block
  [ 8] 0x080a0000, // no data transfer, no index check, check response crc, 48-bit response without busy, no multi-block
  [11] 0x0b0a0000, // no data transfer, no index check, check response crc, 48-bit response without busy, no multi-block
  [17] 0x112a0010, // data read, no index check, check response crc, 48-bit response without busy, no multi-block
  [18] 0x122a0036, // data read, no index check, check response crc, 48-bit response without busy, multi-block, send cmd12 when blocks received
  [55] 0x370a0000  // no data transfer, no index check, check response crc, 48-bit response without busy, no multi-block
};

// Known app commands, must be preceded by a cmd[55]
static uint32_t const acmds[] = {
  [ 6] 0x06020000, // no data transfer, no index check, no crc check, 48-bit response without busy, no multi-block
  [41] 0x29020000, // no data transfer, no index check, no crc check, 48-bit response without busy, no multi-block
  [51] 0x33220010  // data read, no index check, no crc check, 48-bit response without busy, no multi-block
};

static uint32_t response[4];

void emmc_interrupt()
{
  uint32_t new_interrupts = devices.emmc.INTERRUPT;
if (mapped_memory != 0) { mapped_memory[7] = new_interrupts; mapped_memory[6]++; }

  if (new_interrupts != 0) {
    debug_last_interrupts = new_interrupts;
    debug_all_interrupts |= new_interrupts;
    debug_interrupted ++;

    memory_write_barrier(); // About to write to devices.emmc
    devices.emmc.INTERRUPT = new_interrupts; // Acknowledge interrupts.

    if (0 != (new_interrupts & 0x00000001)) { // Command complete
      response[0] = devices.emmc.RESP0;
      if (0x10000 == (current_cmdtm & 0x30000)) {
        response[1] = devices.emmc.RESP1;
        response[2] = devices.emmc.RESP2;
        response[3] = devices.emmc.RESP3;
      }
mapped_memory[32] = response[0];
mapped_memory[33] = response[1];
mapped_memory[34] = response[2];
mapped_memory[35] = response[3];
mapped_memory[36] = current_cmdtm;
mapped_memory[37] = pending_app_cmdtm;
mapped_memory[38] = pending_app_command_arg;
mapped_memory[39] = acmd_expected_state;
mapped_memory[40] = ((response[0] >> 9) & 0xf);
      if (current_cmdtm == cmds[55]) {
        if (((response[0] >> 9) & 0xf) != acmd_expected_state) {
  flush_and_invalidate_cache( (void*) 0x10000, 1024 );
          asm ( "brk 2" );
        }

        if (pending_app_cmdtm != 0) {
          current_cmdtm = pending_app_cmdtm;
          pending_app_cmdtm = 0;
          devices.emmc.ARG1 = pending_app_command_arg;
          devices.emmc.CMDTM = current_cmdtm;
        }
        else {
  flush_and_invalidate_cache( (void*) 0x10000, 1024 );
          asm ( "brk 2" );
        }
      }
      else if (command_thread != 0) {
        wake_thread( command_thread );
      }
      else {
  flush_and_invalidate_cache( (void*) 0x10000, 1024 );
        asm ( "brk 2" );
      }
    }
    if (0 != (new_interrupts & 0x00000002)) { // Data transfer complete
      if (data_thread != 0)
        wake_thread( data_thread );
      else {
        debug_interrupted += 0x100;
  flush_and_invalidate_cache( (void*) 0x10000, 1024 );
        asm( "brk 5" );
      }
    }
    if (0 != (new_interrupts & 0x00000010)) { // FIFO needs data
      // Data write
      for (;;) asm ( "svc 1\nsvc 2\nsvc 5\nsvc 5" ); // Not doing that yet!
      if (data_thread != 0)
        wake_thread( data_thread );
      else debug_interrupted += 0x1000;
    }
    if (0 != (new_interrupts & 0x00000020)) { // FIFO has data
      // Data readable
      for (int i = 0; i < 128; i++) {
        block_data[block_index+i] = devices.emmc.DATA;
      }
      block_index += 128;
      block_size -= 512;
      mapped_memory[42] = block_index;
      mapped_memory[43] = block_size;
      memory_read_barrier(); // Completed our reads of devices.emmc
    }
    if (0 != (new_interrupts & 0x00000100)) { // Card requested interrupt
      asm ( "brk 7" );
    }
  }

  if (0 != (new_interrupts & ~0x00000033)) {
    asm ( "brk 1" );
  }

  memory_read_barrier(); // Completed our reads of devices.emmc
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

static bool power_down( enum device_id id )
{
  enum device_power_state state = power_state( id );

  if (state == POWER_STATE_OFF) {
    uint32_t __attribute(( aligned( 16 ) )) power_on_request[8] =
        { sizeof( power_on_request ), 0,
		0x00028001, 8, 0, id, 2,           // Power down, respond when it's done
                0 };
    // Power down, we'll wait
    mailbox_tag_request( power_on_request );

    if (power_on_request[1] != 0x80000000 || power_on_request[4] != 0x80000008
     || power_on_request[5] != id) {
      return false;
    }
  }
  return true;
}

static bool power_up( enum device_id id )
{
  enum device_power_state state = power_state( id );

  if (state == POWER_STATE_OFF) {
    uint32_t __attribute(( aligned( 16 ) )) power_on_request[8] =
        { sizeof( power_on_request ), 0,
		0x00028001, 8, 0, id, 1,           // Power up
                0 };
    // Power on, we'll wait
    mailbox_tag_request( power_on_request );

    if (power_on_request[1] != 0x80000000 || power_on_request[4] != 0x80000008
     || power_on_request[5] != id) {
      return false;
    }

    // Just to be sure
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


static inline void cmdtm( uint32_t cmd_code, uint32_t arg )
{
  // cmd_code contains the command number, plus what the EMMC device should
  // expect in response.
debug_last_command = cmd_code;

  command_thread = this_thread;
  current_cmdtm = cmd_code;

  devices.emmc.ARG1 = arg;
  devices.emmc.CMDTM = cmd_code;
  dsb();

  wait_until_woken();

  if (cmd_code != cmds[55]) command_thread = 0;
}

static inline void command( int cmd, uint32_t arg )
{
  if (cmd != 0 && cmds[cmd] == 0) {
    asm ( "brk 2" );
  }
  cmdtm( cmds[cmd], arg );
}

static inline void app_command( int acmd, sd_states expected_state, uint32_t shifted_rca, uint32_t arg )
{
  if (acmds[acmd] == 0) {
    asm ( "brk 2" );
  }

  pending_app_cmdtm = acmds[acmd];
  pending_app_command_arg = arg;
  acmd_expected_state = expected_state;

  // When the CMD55 completes, the ACMD will be executed by the interrupt handler
  command( 55, shifted_rca );
}

#define MEMSET_WITHOUT_ASM
#define assert( x ) if (x) {}
#include "memset.h"

static bool set_clock_rate( uint32_t target_rate )
{
  // NOTE: This code is assuming a fixed clock + divider, but the SoC seems
  // to allow programmable clocks; min, max, set, etc. via mailbox i/f.
  // Might be better the other way? (I'm assuming that's what bit 5 means.)

  static uint64_t base_clock_rate_hz = 0;

  if (base_clock_rate_hz == 0) {
    debug_progress = 0x60;
    base_clock_rate_hz = base_clock_rate( CLK_EMMC );
    debug_progress = 0x61;
  }
  // Pi 3: 250MHz

  uint64_t divider = (base_clock_rate_hz + target_rate - 1)/ target_rate;

  if (divider == 0 || divider > 0x3ff) return false;

  while (devices.emmc.STATUS & 3) {
    memory_read_barrier(); // Completed our reads of devices.emmc
    sleep_ms( 1 );
  }

  uint32_t control1 = devices.emmc.CONTROL1;

  if (0 == (control1 & 4)) {
debug_progress = 0x63;
    control1 &= ~4; // Clears CLK_EN
    memory_write_barrier(); // About to write to devices.emmc
    devices.emmc.CONTROL1 = control1;
    memory_read_barrier(); // Completed our reads of devices.emmc
    sleep_ms( 2 );
  }

  control1 |= 0x00000001; // CLK_INTLEN (Clock enable for internal EMMC clocks for power saving)
  control1 &= 0xfff0ffdf; // Clears CLK_GENSEL, DATA_TOUNIT
  control1 |= 0x000e0000; // Set timeout to maximum

  control1 &= 0xffff003f; // Clears CLK_FREQ8, CLK_FREQ_MS2 (divider)
  control1 |= ((divider & 0xff) << 8) | ((divider & 0x300) >> 2);

  memory_write_barrier(); // About to write to devices.emmc
  devices.emmc.CONTROL1 = control1;
  sleep_ms( 2 );

  control1 |= 4;          // Enable clock
  memory_write_barrier(); // About to write to devices.emmc
  devices.emmc.CONTROL1 = control1;
  sleep_ms( 2 );

debug_progress = 0x62;
  return true;
}

static bool prepare_for_interrupts_from_sd_port()
{
  memory_write_barrier(); // About to write to devices.interrupts
  devices.interrupts.Enable_IRQs_2 = 0x40000000; // Arasan interrupt.
  dsb();
  return true;
}

static bool initialise_mmc_hardware()
{
  memory_write_barrier(); // About to write to devices.emmc

  uint32_t control1 = devices.emmc.CONTROL1;
  memory_read_barrier(); // Completed our reads of devices.emmc

  devices.emmc.CONTROL0 = 0; 
  devices.emmc.CONTROL2 = 0;
  control1 |=  (1 << 24); // SRST_HC (Reset the complete host circuit)
  control1 &= ~(1 <<  2); // CLK_EN (SD clock enable)
  control1 &= ~(1 <<  0); // CLK_INTLEN (internal clocks enable)
  devices.emmc.CONTROL1 = control1;

  while (0 != (devices.emmc.CONTROL1 & (1 << 24))) {
    memory_read_barrier(); // Completed our reads of devices.emmc
    sleep_ms( 1 );
  }

  return true;
}

static bool initialise_sd_interface()
{
  debug_progress = 8;
  if (!power_up( SD_Card )) return false;

  debug_progress = 9;
  if (!initialise_mmc_hardware()) return false;

  debug_progress = 10;

  // set to 400KHz, for identification phase
  if (!set_clock_rate( 400000 )) return false;

  debug_progress = 11;

  // All known interrupts, I like interrupts!
  // Not all known. According to circle/addon/SDCard/emmc.cpp, bits 6 and 7 are insertion and removal
  // git clone https://github.com/rsta2/circle.git
  // On startup, bit 6 is set (card is inserted; level sensitive?)

  devices.emmc.IRPT_MASK = 0x017f71b7;
  devices.emmc.IRPT_EN   = 0x017f71b7;

  devices.emmc.SPI_INT_SPT   = 0xff; // Interrupt independent of card select line

  while (0 == (devices.emmc.CONTROL1 & (1 << 1))) { // CLK_STABLE
    memory_read_barrier(); // Completed our reads of devices.emmc
    sleep_ms( 1 );
  }

  debug_progress = 12;

  return true;
}

struct {
  union {
    struct {
      uint32_t res0:8;
      uint32_t ocr:16;
      uint32_t s18:1;
      uint32_t res1:3;
      uint32_t xpc:1;
      uint32_t fb:1;
      uint32_t sdhc:1;
      uint32_t busy:1;
    };
    uint32_t acmd41_response;
  };
  union {
    struct {
      uint64_t res2:8;
      uint64_t mdt:12;
      uint64_t res3:4;
      uint64_t psn:32;
      uint64_t prv:8;
      uint64_t pnm:40;
      uint64_t oid:16;
      uint64_t mid:8;
    };
    uint32_t cid[4];
  };
  union {
    struct {
      uint64_t manufacturer:32;
      uint64_t CMD_SUPPORT:4;
      uint64_t res4:2;
      uint64_t SD_SPECX:4;
      uint64_t SD_SPEC4:1;
      uint64_t EX_SECURITY:3;
      uint64_t SD_SPEC3:1;
      uint64_t SD_BUS_WIDTHS:4;
      uint64_t SD_SECURITY:3;
      uint64_t DATA_STAT_AFTER_ERASE:1;
      uint64_t SD_SPEC:4;
      uint64_t SCR_STRUCTURE:4;
    };
    uint32_t scr[2];
  };
} current_device = {};

static inline bool current_device_bits_match()
{
  uint32_t old0 = current_device.scr[0];
  uint32_t old1 = current_device.scr[1];
  current_device.scr[0] = 0x12345678;
  current_device.scr[1] = 0x9abcdef0;
  bool result = current_device.manufacturer == 0x12345678; // && current_device.SD_BUS_WIDTHS == 8;
  current_device.scr[0] = old0;
  current_device.scr[1] = old1;
  return result;
}

static bool switch_to_1v8_signalling()
{
  command( 11, 0 ); // Voltage switch

  memory_write_barrier(); // About to write to devices.emmc
  devices.emmc.CONTROL1 &= ~4;
  memory_read_barrier(); // Completed our reads of devices.emmc
  sleep_ms( 5 ); // Why 5? Circle code.

  uint32_t status = devices.emmc.STATUS;
  memory_read_barrier(); // Completed our reads of devices.emmc

  if (0 != (status & 0xf00000)) return false; // DAT lines should settle to 0

  memory_write_barrier(); // About to write to devices.emmc
  devices.emmc.CONTROL0 |= (1 << 8); // Not in the bcm2835 documentation!? 1.8V

  if (0xf00000 != (status & 0xf00000)) return false; // DAT lines should settle to 0xf

  return true;
}

static bool identify_device()
{
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
    sleep_ms( 1 );
  }

  debug_progress = 13;

  // Device: Go idle
  command( 0, 0 );

  // Device: Send Interface Condition
  command( 8, 0x1aa ); // Voltage supplied: 2.7-3.6V, recommended check pattern

  // TODO Check for HC Check for reponse?
  // The important part of "48-bit" responses are returned in RESP0
  // The hardware checks the CRC, etc.
  if (0x1aa != (0xfff & response[0])) {
    return false;
  }

  debug_progress = 16;

  // Inform device of Host Capacity support, receive operating condition register
  // 4.2.3.1 Initialization Command (ACMD41)
  // "inquiry CMD41"
  app_command( 41, idle, 0, 0 ); // Initial ACMD41, requesting OCR from device

  bool initialised = false;
  while (!initialised) {
    // "first ACMD41", repeated verbatim until powered up
    // This request always requests 1.8V signalling, no support for cards that don't, atm
    app_command( 41, idle, 0, 0x40ff8000 );
    //app_command( 41, idle, 0, 0x41ff8000 );

    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 10 ), N( response[0] ), N( 0xfff0f0f0 ) );

    current_device.acmd41_response = response[0];

    initialised = (0 != current_device.busy);

    if (!initialised) sleep_ms( 1 );
  }

  if (!current_device.sdhc) { asm ( "brk 1" ); }

  // Card should now be in ready mode
  // This is where we change the voltage, clock speed, etc.

  if (current_device.s18) { // Supports 1.8V signalling, use it!
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 10 ), N( 0x88888888 ), N( 0xffffffff ) ); asm( "svc 0" );
    if (!switch_to_1v8_signalling()) {
      // Timeout, or other failure to switch
      // Power cycle, and retry without 1.8V signalling
      asm ( "brk 1" ); 
    }
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 10 ), N( 0x99999999 ), N( 0xffffffff ) ); asm( "svc 0" );
  }

  command( 2, 0 ); // ALL_SEND_CID
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
  current_device.cid[0] = response[0];
  current_device.cid[1] = response[1];
  current_device.cid[2] = response[2];
  current_device.cid[3] = response[3];

  // Card should now be in ident mode
  uint32_t shifted_rca = 0;
  // This loops, in case the card returns a zero address, which is invalid, so we'll ask for another...
  while (shifted_rca == 0) {
    command( 3, 0 );
    shifted_rca = response[0] & 0xffff0000;
    if (ident != ((response[0] >> 9) & 0xf)) {
      // Wasn't in ident when receiving the command?
      asm ( "brk 2" );
    }
  }
  // Was in ident mode, now stby.

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 10 ), N( 0xaaaaaaaa ), N( 0xffffffff ) ); asm( "svc 0" );

  debug_progress = 17;

  // For some reason, the Pi generates a DATA_DONE interrupt in response to this command
  data_thread = this_thread;

  command( 7, shifted_rca ); // Address the card
  // Was in stby mode, now tran.

  wait_until_woken();
  data_thread = 0;

  debug_progress = 37;

  if (0 != (response[0] & (1 << 25))) {
    // Should do CMD42
    asm ( "brk 2" );
    return false;
  }

  // About to set bus width
  devices.emmc.CONTROL0 |= 2; // 4-bit bus
  app_command( 6, tran, shifted_rca, 2 ); // Set bus width 4 bits

  // Try reading some data
  data_thread = this_thread;

  devices.emmc.BLKSIZECNT = (1 << 16) | 8; // 1 block of 8 bytes
  block_index = 0;
  block_size = 8;
  block_data = current_device.scr;

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 10 ), N( 0xbbbbbbbb ), N( 0xffffffff ) ); asm( "svc 0" );
  debug_progress = 18;

  app_command( 51, tran, shifted_rca, 0 );
  wait_until_woken(); // As data_thread...

  debug_progress = 19;

  if (!set_clock_rate( 25000000 )) return false;

  // TODO: Use DDR50. This takes a little over a second to read the 5MB RISCOS.IMG (contiguous on disc)

  debug_progress = 0x55;

  return true;
}

typedef struct EMMC { 
  uint64_t count;
} *EMMC;

static struct EMMC emmc_service_singleton = { 0 };
uint64_t __attribute__(( aligned(16) )) emmc_stack[64];
uint64_t emmc_lock = 0;

ISAMBARD_INTERFACE( BLOCK_DEVICE )
#include "interfaces/provider/BLOCK_DEVICE.h"

ISAMBARD_BLOCK_DEVICE__SERVER( EMMC )
ISAMBARD_PROVIDER( EMMC, AS_BLOCK_DEVICE( EMMC ) )
ISAMBARD_PROVIDER_SHARED_LOCK_AND_STACK( EMMC, RETURN_FUNCTIONS_BLOCK_DEVICE( EMMC ), emmc_lock, emmc_stack, 64 * 8 )

extern PHYSICAL_MEMORY_BLOCK test_memory;

uint32_t initialisation_thread = -1;

void show_page_thread()
{
  mapped_memory[0] = 0;
  mapped_memory[1] = 0;

  tnd = TRIVIAL_NUMERIC_DISPLAY__get_service( "Trivial Numeric Display", -1 );

    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 800 ), N( 10 ), N( initialisation_thread ), N( 0xfff0f0f0 ) );
  debug_progress = 0x22;
  wake_thread( initialisation_thread );
  debug_progress = 0x44;

  TRIVIAL_NUMERIC_DISPLAY__set_page_to_show( tnd, test_memory, NUMBER__from_pointer( mapped_memory ) );
  for (;;) {
    mapped_memory[1] ++;
    sleep_ms( 20 );
    TRIVIAL_NUMERIC_DISPLAY__show_page( tnd );
    TRIVIAL_NUMERIC_DISPLAY__show_8bits( tnd, N( 10 ), N( 10 ), N( debug_progress ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 32 ), N( 10 ), N( debug_last_command ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 132 ), N( 10 ), N( debug_last_acommand ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 232 ), N( 10 ), N( debug_last_interrupts ), N( 0xfff0f0f0 ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 332 ), N( 10 ), N( debug_all_interrupts ), N( 0xffffffff ) );
    TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 432 ), N( 10 ), N( debug_interrupted ), N( 0xfff0f0f0 ) );
    // base_clock_rate( CLK_EMMC );
  }
}

void __attribute__(( noreturn )) hammer_tag_interface()
{
  long long int volatile local = 0x222211110000;
  long long int volatile code = 2 + (0x7 & ((uint64_t) (&local) >> 8));
  long long int volatile rate = 0x12345678;
  for (;;) {
    local++;
    rate = base_clock_rate( code );
    if (rate < 100) { asm( "brk 1" ); }
    sleep_ms( 50 );
  }
}

uint64_t ping = 0;
uint64_t pong = 0;
uint64_t pingpong = 0;
uint64_t pingpong_lock = 0;

#include "exclusive.h"

uint32_t volatile pit = 0;
uint32_t volatile pot = 0;

void __attribute__(( noreturn )) ping_thread()
{
  int volatile local = 0;
  yield();
  pit = this_thread;
  yield();
  if (pot == 0) asm ( "brk 3" );
  for (;;) {
    local++;
    wait_until_woken();
    ping++;
    pingpong--;
    wake_thread( pot );
  }
}

void __attribute__(( noreturn )) pong_thread()
{
  int volatile local = 0;
  yield();
  pot = this_thread;
  yield();
  if (pit == 0) asm ( "brk 3" );
  // Kick it off!
  wake_thread( pit );
  for (;;) {
    local++;
    wait_until_woken();
    yield();
    pong++;
    pingpong++;
    wake_thread( pit );
  }
}

uint32_t time = 0;

void timer_event()
{
  if (time != 0) wake_thread( time );
}

void __attribute__(( noreturn )) timer_thread()
{
  int volatile local = 0;
  int volatile total = 0;
  time = this_thread;

  memory_write_barrier(); // About to write to devices.timer
  //devices.timer.load = 96; // .1ms.  2s: 1920000;
  devices.timer.load = 1920; // 2ms
  // devices.timer.load = 10; // ~100us.  Too much! Can't keep up.
  //devices.timer.load = 48; // ~200us.
  devices.timer.control |= 0x2a2; // Interrupts enabled, but see bit 0 of Enable_Basic_IRQs

  memory_write_barrier(); // About to write to devices.interrupts
  devices.interrupts.Enable_Basic_IRQs = 1; // Enable "ARM Timer" IRQ

  for (;;) {
    local++;
    total += 1 + wait_until_woken();
  }
}

static void start_show_page_thread()
{
  initialisation_thread = this_thread;

#if 0
  static struct {
    uint64_t stack[64];
  } __attribute__(( aligned(16) )) stack = {};

  create_thread( show_page_thread, (uint64_t*) ((&stack)+1) );
#else
  create_thread( show_page_thread, (uint64_t*) (0x11000) );
#if 0
  create_thread( ping_thread, (uint64_t*) 0x10700 );
  create_thread( pong_thread, (uint64_t*) 0x10800 );

  create_thread( timer_thread, (uint64_t*) 0x10a00 );

  for (int i = 1; i < 4; i++) {
    create_thread( hammer_tag_interface, (uint64_t*) (0x10f00ull - (i * 0x100)) );
  }
#endif
#endif

  wait_until_woken(); // Show page thread is running
}

void expose_emmc()
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

  if (!identify_device()) return;

  debug_progress = 21;
  EMMC__BLOCK_DEVICE__register_service( "EMMC", &emmc_service_singleton );
  debug_progress = 22;
}

void EMMC__BLOCK_DEVICE__read_4k_pages( EMMC o, PHYSICAL_MEMORY_BLOCK dest, NUMBER first_block )
{
  o = o;

  debug_progress = 0x40;
  if (PHYSICAL_MEMORY_BLOCK__is_read_only( dest ).r) {
    EMMC__exception( 0 ); // name_code( "Memory not writable" ) );
  }
  uint32_t size = PHYSICAL_MEMORY_BLOCK__size( dest ).r;
  uint32_t start_pa = PHYSICAL_MEMORY_BLOCK__physical_address( dest ).r;
  data_thread = this_thread;

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 10 ), N( 0x22222222 ), N( 0xffffffff ) );

#define dodma
#ifndef dodma
  block_data = (void *) (6 << 20);
  block_index = 0;
  block_size = size;

  DRIVER_SYSTEM__map_at( driver_system(), dest, NUMBER__from_pointer( block_data ) );
#else
  memory_write_barrier(); // About to write to devices.emmc
  // Disable data interrupt, from now on using DMA
  devices.emmc.IRPT_MASK = 0x017f7197;
  devices.emmc.IRPT_EN   = 0x017f7197;

  struct DMA_Control {
    uint32_t TI; // Transfer Information
    uint32_t SOURCE_AD;
    uint32_t DEST_AD;
    uint32_t TXFR_LEN;
    uint32_t STRIDE;
    uint32_t NEXTCONBK;
  } __attribute__(( aligned( 32 ) )) dma_cntrl;

  static uint64_t fake_source = 0x4343424274745252ull;
  uint32_t source_pa = DRIVER_SYSTEM__physical_address_of( driver_system(), NUMBER__from_pointer( &fake_source ) ).r;

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 10 ), N( source_pa ), N( 0xfffff000 ) );
  dma_cntrl.TI = ( 1 << 26) // Not wide bursts
               | ( 0 << 21) // Waits
               | ( 0 << 16) // Peripheral: None; not needed for AXI peripherals
               | ( 0 << 12) // Burst Transfer Length
               | ( 0 << 11) // Source ignore (zero) All fives?
               | ( 0 << 10) // Use DREQ for source (0 => use AXI bus?)
               | ( 0 <<  9) // Src width 0 = 32bit, 1 = 128 bit
               | ( 0 <<  8) // Src increment (4 or 32 bytes)
               | ( 0 <<  5) // Destination width  0 = 32bit, 1 = 128 bit
               | ( 1 <<  4) // Destination increment
               | ( 1 <<  3) // Wait for AXI write responses
               | ( 0 <<  1) // 2D Mode
               | ( 1 <<  0);// Interrupt enable
  // It looks like the GPU still only sees 1GB, aliased at 0x00000000, 0x40000000, 0x80000000, and 0xc0000000.
  // Experimentally, DMA from the 0 alias contains zero bytes, not what was loaded from the SD card on boot.
  // From the 4 alias gets the correct data, but only every second destination 64-bit word is filled
  dma_cntrl.SOURCE_AD = source_pa | 0x80000000; // 0xc0000000 | 0x3f300020; // devices.emmc.DATA Unchached bus address?
  dma_cntrl.DEST_AD = start_pa;
  dma_cntrl.TXFR_LEN = size;
  dma_cntrl.STRIDE = 0;
  dma_cntrl.NEXTCONBK = 0;
  uint32_t cs = ( 1 << 28) // Wait for remaining writes (over AXI bus)
              | ( 1 <<  4) // Destination increment
;
  NUMBER pa = DRIVER_SYSTEM__physical_address_of( driver_system(),
                NUMBER__from_pointer( &dma_cntrl ) );
  flush_and_invalidate_cache( (void*) &dma_cntrl, sizeof( dma_cntrl ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 50 ), N( dma_cntrl.TI ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 60 ), N( dma_cntrl.SOURCE_AD ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 70 ), N( dma_cntrl.DEST_AD ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( 80 ), N( dma_cntrl.TXFR_LEN ), N( 0xffff00ff ) );

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 10 ), N( 0x44444444 ), N( 0xffffffff ) );
if (0) {
  // Request available DMA channels (presumably others are used by GPU)
  uint32_t __attribute(( aligned( 16 ) )) request[7] = { sizeof( request ), 0, 0x00060001, 4, 0, 0, 0 };
  mailbox_tag_request( request );

  if (request[1] != 0x80000000 || request[4] != 0x80000004) {
    asm ( "brk 9" );
  }
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1700 ), N( 100 ), N( request[5] ), N( 0xff0000ff ) ); asm( "svc 0" );
if ((request[5] & 1) == 0) {
  asm ( "svc 0" );
  for (;;) {}  // Actually returns 7f35, so dma 0 is available...
}
}

uint32_t int_status = devices.dmactl.INT_STATUS;
uint32_t int_enable = devices.dmactl.ENABLE;
uint32_t control = devices.dma[0].CS;
uint32_t remaining = devices.dma[0].TXFR_LEN;
  memory_read_barrier();

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 50 ), N( int_status ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 60 ), N( int_enable ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 70 ), N( control ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( 80 ), N( remaining ), N( 0xffff00ff ) );

  memory_write_barrier(); // About to write to devices.dma
  devices.dma[0].CONBLK_AD = pa.r;
  devices.dma[0].CS = 1;

int_status = devices.dmactl.INT_STATUS;
int_enable = devices.dmactl.ENABLE;
control = devices.dma[0].CS;
remaining = devices.dma[0].TXFR_LEN;
  memory_read_barrier();

TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 50 ), N( int_status ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 60 ), N( int_enable ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 70 ), N( control ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 80 ), N( remaining ), N( 0xffff00ff ) );
#endif

  // Kick off transfer for the DMA to put into memory
  memory_write_barrier(); // About to write to devices.emmc
  devices.emmc.BLKSIZECNT = ((size / 512) << 16) | 512; // blocks of 512 bytes

  data_thread = this_thread;

  command( 18, first_block.r );

int y = 140;
do {
int_status = devices.dmactl.INT_STATUS;
int_enable = devices.dmactl.ENABLE;
control = devices.dma[0].CS;
remaining = devices.dma[0].TXFR_LEN;
uint32_t src = devices.dma[0].SOURCE_AD;
uint32_t dst = devices.dma[0].DEST_AD;
  memory_read_barrier();
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 50 ), N( int_status ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 60 ), N( int_enable ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( 70 ), N( control ), N( 0xffff00ff ) );

if (y > 760) y = 140;
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1600 ), N( y ), N( remaining ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1400 ), N( y ), N( src ), N( 0xffff00ff ) );
TRIVIAL_NUMERIC_DISPLAY__show_32bits( tnd, N( 1500 ), N( y ), N( dst ), N( 0xffff00ff ) );
y += 10;
asm ("svc 0" );
sleep_ms( 1 );
} while ((control & 2) == 0);

  // wait_until_woken(); // As data thread

  // if (block_index != size/4) {
    // EMMC__exception( 0 );
  // }

  EMMC__BLOCK_DEVICE__read_4k_pages__return();
}

