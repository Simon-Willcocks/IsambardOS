extern struct {
  union {
    struct {
      uint32_t       control;
      uint32_t       res1;
      uint32_t       timer_prescaler;
      uint32_t       GPU_interrupts_routing;
      uint32_t       Performance_Monitor_Interrupts_routing_set;
      uint32_t       Performance_Monitor_Interrupts_routing_clear;
      uint32_t       res2;
      uint32_t       Core_timer_access_LS_32_bits; // Access first when reading/writing 64 bits.
      uint32_t       Core_timer_access_MS_32_bits;
      uint32_t       Local_Interrupt_routing0;
      uint32_t       Local_Interrupts_routing1;
      uint32_t       Axi_outstanding_counters;
      uint32_t       Axi_outstanding_IRQ;
      uint32_t       Local_timer_control_and_status;
      uint32_t       Local_timer_write_flags;
      uint32_t       res3;
      uint32_t       Core_timers_Interrupt_control[4];
      uint32_t       Core_Mailboxes_Interrupt_control[4];
      uint32_t       Core_IRQ_Source[4];
      uint32_t       Core_FIQ_Source[4];
      struct {
        uint32_t       Mailbox[4]; // Write only!
      } Core_write_set[4];
      struct {
        uint32_t       Mailbox[4]; // Read/write
      } Core_write_clear[4];
    } QA7;
    struct { uint32_t page[1024]; };
  };
} volatile __attribute__(( aligned( 4096 ) )) device_pages;
