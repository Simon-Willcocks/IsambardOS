/* Copyright (c) 2020 Simon Willcocks */

typedef struct Core Core;

void __attribute__(( noreturn, noinline )) c_el3_nommu( Core *core, int number )
{
  for (;;) {
    *(char volatile *) 0x3f201000 = '0' + number;
    for (int i = 0; i < number * 1000000; i++) {
    }
  }
}
