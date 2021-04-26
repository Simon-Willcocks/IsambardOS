/* Copyright (c) 2020 Simon Willcocks */

/* Services provided by the kernel only to the system driver for each core. */

enum Isambard_Special_Request {
  Isambard_System_Service_Set_Interrupt_Thread = 1
          // Identify the thread that will run when a normal interrupt occurs
, Isambard_System_Service_Add_Device_Page
          // Identify a device page to be mapped into the system map (e.g. to access a GIC, timer, or GPIO)
, Isambard_System_Service_Updated_Map
          // Re-load the system map that has been updated in another core (initialisation only)

, Isambard_System_Service_CreateMap
, Isambard_System_Service_NextDriver

, Isambard_System_Service_ReadInterface

, Isambard_System_Service_ReadHeap
, Isambard_System_Service_WriteHeap
, Isambard_System_Service_AllocateHeap
, Isambard_System_Service_FreeHeap

, Isambard_System_Service_Allocate_Thread
, Isambard_System_Service_Release_Thread
, Isambard_System_Service_Thread_Now_Runnable
, Isambard_System_Service_Thread_Next_Runnable
, Isambard_System_Service_Thread_Last_Runnable

, Isambard_System_Service_Thread_Make_Partner
, Isambard_System_Service_Thread_Switch_To_Partner

, Isambard_System_Service_Create_Thread
};

// Entry points into System driver, known only to the kernel and the driver
enum {
  System_Enter_Core0 = 0,
  System_Service_ThreadExit = 4,
  System_Service_Map = 8,
  System_Service_PhysicalMemoryBlock = 12
};

// The only system service intercepted by the kernel. Needs EL1 to be efficient
enum { DRIVER_SYSTEM_physical_address_of = 0x4a274f85 };

// Structures known to both the kernel and the system driver

// Packed objects
typedef union {
  uint64_t r;
  struct __attribute__(( packed )) {
    uint64_t start_page:24;     // Max 16GB memory
    uint64_t page_count:20;     // Max 4GB memory in one block
    uint64_t read_only:1;
    uint64_t reserved:15;
    uint64_t is_subpage:1;      // There's another CMB which includes this one
    uint64_t memory_type:3;     // index into MAIR
  };
} ContiguousMemoryBlock;

typedef union {
  uint64_t r;
  struct __attribute__(( packed )) {
    uint64_t start_page:24; // Max 16GB memory
    uint64_t page_count:20;  // Max 4GB memory in one block
    uint64_t read_only:1;    // combined with physical permissions
    uint64_t executable:1;   //  ditto
    uint64_t memory_block:18; // interface index
  };
} VirtualMemoryBlock;

typedef union {
  uint64_t r;
  struct __attribute__(( packed )) {
    uint64_t heap_offset_lsr4:32;
    uint64_t map_object:20;
    uint64_t number_of_vmbs:12;
  };
} MapValue;

