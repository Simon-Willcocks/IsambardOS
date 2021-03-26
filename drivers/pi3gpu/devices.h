#include "drivers.h"

#define STACK_SIZE 64

extern struct __attribute__(( packed )) {
  uint32_t unused1[0x80]; // ... 0x200
  struct __attribute__(( packed )) {
    uint32_t IRQ_basic_pending; // 0x200
    uint32_t IRQ_pending_1; // 0x204
    uint32_t IRQ_pending_2; // 0x208
    uint32_t FIQ_control; // 0x20C
    uint32_t Enable_IRQs_1; // 0x210
    uint32_t Enable_IRQs_2; // 0x214
    uint32_t Enable_Basic_IRQs; // 0x218
    uint32_t Disable_IRQs_1; // 0x21C
    uint32_t Disable_IRQs_2; // 0x220
    uint32_t Disable_Basic_IRQs; // 0x224
  } interrupts;
  uint32_t unused2[0x80 - 10]; // ... 0x400
  struct __attribute__(( packed )) {
    uint32_t load; // 0x400
    uint32_t ro_value; // 0x404
    uint32_t control; // 0x408
    uint32_t wo_irq_clear; // 0x40c
    uint32_t ro_raw_irq; // 0x410
    uint32_t ro_masked_irq; // 0x414
    uint32_t reload; // 0x418
    uint32_t pre_divider; // 0x41c
    uint32_t free_running_counter; // 0x41c
  } timer; // This is the "Timer (ARM side)", p.196 of the BCM2835 peripherals pdf
  uint32_t unused3[0x80 - 9]; // ... 0x600
  uint32_t unused4[0x80]; // ... 0x800
  uint32_t unused5[0x20]; // ... 0x880
  struct {
    uint32_t value; // Request or Response, depending if from or to ARM,
               // (Pointer & 0xfffffff0) | Channel 0-15
    uint32_t res1;
    uint32_t res2;
    uint32_t res3;
    uint32_t peek;  // Presumably doesn't remove the value from the FIFO
    uint32_t sender;// ??
    uint32_t status;// bit 31: Tx full, 30: Rx empty
    uint32_t config;
  } mailbox[2]; // ARM may read mailbox 0, write mailbox 1.
  uint32_t unused6[0x50]; // ... 0xa00
  uint32_t unused7[0x80 * 3]; // ... 0x1000

  struct {
    uint32_t ARG2;                     // 0x0  ACMD23 Argument
    uint32_t BLKSIZECNT;               // 0x4  Block Size and Count
    uint32_t ARG1;                     // 0x8  Argument
    uint32_t CMDTM;                    // 0xc  Command and Transfer Mode
    uint32_t RESP0;                    // 0x10 Response bits 31 : 0
    uint32_t RESP1;                    // 0x14 Response bits 63 : 32
    uint32_t RESP2;                    // 0x18 Response bits 95 : 64
    uint32_t RESP3;                    // 0x1c Response bits 127 : 96
    uint32_t DATA;                     // 0x20 Data
    uint32_t STATUS;                   // 0x24 Status
    uint32_t CONTROL0;                 // 0x28 Host Configuration bits
    uint32_t CONTROL1;                 // 0x2c Host Configuration bits
    uint32_t INTERRUPT;                // 0x30 Interrupt Flags
    uint32_t IRPT_MASK;                // 0x34 Interrupt Flag Enable
    uint32_t IRPT_EN;                  // 0x38 Interrupt Generation Enable
    uint32_t CONTROL2;                 // 0x3c Host Configuration bits
    uint32_t space1[(0x50 - 0x40)/4];
    uint32_t FORCE_IRPT;               // 0x50 Force Interrupt Event
    uint32_t space2[(0x70 - 0x54)/4];
    uint32_t BOOT_TIMEOUT;             // 0x70 Timeout in boot mode
    uint32_t DBG_SEL;                  // 0x74 Debug Bus Configuration
    uint32_t space3[(0x80 - 0x78)/4];
    uint32_t EXRDFIFO_CFG;             // 0x80 Extension FIFO Configuration
    uint32_t EXRDFIFO_EN;              // 0x84 Extension FIFO Enable
    uint32_t TUNE_STEP;                // 0x88 Delay per card clock tuning step
    uint32_t TUNE_STEPS_STD;           // 0x8c Card clock tuning steps for SDR
    uint32_t TUNE_STEPS_DDR;           // 0x90 Card clock tuning steps for DDR
    uint32_t space4[(0xf0 - 0x94)/4];
    uint32_t SPI_INT_SPT;              // 0xf0 SPI Interrupt Support
    uint32_t space5[(0xfc - 0xf4)/4];
    uint32_t SLOTISR_VER;              // 0xfc Slot Interrupt Status and Version
    uint32_t unused[0xf00 / 4]; // ... 0x1000
  } emmc;

  union {
  struct { // The GPIO has 41 registers. All accesses are assumed to be 32-bit.
    uint32_t GPFSEL[6]; // 0x0000 GPIO Function Select R/W
    uint32_t unused1[1];
    uint32_t GPSET[2]; // 0x001C GPIO Pin Output Set W
    uint32_t unused2[1];
    uint32_t GPCLR[2]; // 0x0028 GPIO Pin Output Clear W
    uint32_t unused3[1];
    uint32_t GPLEV[2]; // 0x0034 GPIO Pin Level R
    uint32_t unused4[1];
    uint32_t GPEDS[2]; // 0x0040 GPIO Pin Event Detect Status R/W
    uint32_t unused5[1];
    uint32_t GPREN[2]; // 0x004C GPIO Pin Rising Edge Detect Enable R/W
    uint32_t unused6[1];
    uint32_t GPFEN[2]; // 0x0058 GPIO Pin Falling Edge Detect Enable R/W
    uint32_t unused7[1];
    uint32_t GPHEN[2]; // 0x0064 GPIO Pin High Detect Enable R/W
    uint32_t unused8[1];
    uint32_t GPLEN[2]; // 0x0070 GPIO Pin Low Detect Enable R/W
    uint32_t unused9[1];
    uint32_t GPAREN[2]; // 0x007C GPIO Pin Async. Rising Edge Detect R/W
    uint32_t unused10[1];
    uint32_t GPAFEN[2]; // 0x0088 GPIO Pin Async. Falling Edge Detect R/W
    uint32_t unused11[1];
    uint32_t GPPUD; // 0x0094 GPIO Pin Pull-up/down Enable R/W
    uint32_t GPPUDCLK[2]; // 0x0098 GPIO Pin Pull-up/down Enable Clock R/W
    uint32_t unused12[4];
    uint32_t TEST; // 0x00B0 Test 4 R/W
  } gpio;
  uint32_t res0000[0x40];
  };
  union {
  uint32_t res0100[0x40]; // PL011 USRT
  struct {
    uint32_t data;
    uint32_t RSRECR;
    uint32_t FR;
    uint32_t ILPR;
    uint32_t IBRD;
    uint32_t LCRH;
  } PL011_USRT;
  };
  uint32_t res0200[0x40];
  uint32_t res0300[0x40];
  uint32_t res0400[0x40];
  uint32_t res0500[0x40];
  uint32_t res0600[0x40];
  uint32_t res0700[0x40];
  uint32_t res0800[0x40];
  uint32_t res0900[0x40];
  uint32_t res0a00[0x40];
  uint32_t res0b00[0x40];
  uint32_t res0c00[0x40];
  uint32_t res0d00[0x40];
  uint32_t res0e00[0x40];
  uint32_t res0f00[0x40];

  struct {
    uint32_t control_status;
    uint32_t low;
    uint32_t high;
    uint32_t compare[4];
    uint32_t unused[1024-7];
  } system_timer;

} volatile  __attribute__(( aligned(4096) )) devices;

extern void emmc_interrupt();
extern void mailbox_interrupt();

extern void expose_gpu_mailbox();
extern void expose_frame_buffer();
extern void expose_emmc();

extern void mailbox_tag_request( uint32_t *request );

extern void sleep_ms( uint64_t ms );

static void dsb() { asm volatile ( "dsb sy" ); }

