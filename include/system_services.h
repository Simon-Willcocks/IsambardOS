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

, Isambard_System_Service_Create_Thread
};

// Entry points into System driver, known only to the kernel and the driver
enum {
  System_Enter_Core0 = 0,
  System_Service_ThreadExit = 4,
  System_Service_Map = 8,
  System_Service_PhysicalMemoryBlock = 12
};

enum { DRIVER_SYSTEM_physical_address_of = 0x4a274f85 };

