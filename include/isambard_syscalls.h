// Copyright (c) Simon Willcocks 2021

#define ISAMBARD_GATE 0xf001
#define ISAMBARD_DUPLICATE_TO_RETURN 0xf002
#define ISAMBARD_DUPLICATE_TO_PASS 0xf003
#define ISAMBARD_INTERFACE_TO_RETURN 0xf004
#define ISAMBARD_INTERFACE_TO_PASS 0xf005

#define ISAMBARD_LOCK_WAIT 0xf006
#define ISAMBARD_LOCK_RELEASE 0xf007

#define ISAMBARD_YIELD 0xf008

#define ISAMBARD_CALL 0xf009
#define ISAMBARD_RETURN 0xf00a
#define ISAMBARD_EXCEPTION 0xf00b

#define ISAMBARD_SWITCH_TO_PARTNER 0xf00c
#define ISAMBARD_GET_PARTNER_REG 0xf00d
#define ISAMBARD_SET_PARTNER_REG 0xf00e
// FIXME: Should be done through a system driver interface, maybe the above 2, as well
#define ISAMBARD_CHANGE_VM_SYSTEM_REGISTER 0xf00f

// Only usable by system driver:
#define ISAMBARD_SYSTEM_REQUEST 0xf010
