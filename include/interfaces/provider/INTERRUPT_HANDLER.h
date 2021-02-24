#define ISAMBARD_INTERRUPT_HANDLER__SERVER( c ) \
static void c##__INTERRUPT_HANDLER__interrupt( c o, unsigned call ); \
REGISTER c##_call_handler( c o, unsigned call, REGISTER p1, REGISTER p2, REGISTER p3 ) \
{\
  switch (call) {\
  case 0x78bdc371: c##__INTERRUPT_HANDLER__interrupt( o, call ).r; \
  }\
}

