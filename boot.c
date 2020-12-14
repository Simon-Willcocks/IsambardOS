/* Copyright (c) 2020 Simon Willcocks */

// Establish a stack for each core, at the top of an anonymous struct Core, and call a C routine.
//
// The size of the Core structure must be defined at compilation as CORE_SIZE (I recommend a multiple of 4k)

typedef struct Core Core;

void __attribute__(( noreturn, noinline )) c_el3_nommu( Core *core, int number );

asm ( ".section .init"
    "\n.global  _start"
    "\n.type    _start, %function"
    "\n_start:"
    "\n\tmrs     x0, mpidr_el1"
    "\n\tmov     x3, #"CORE_SIZE
    "\n\ttst     x0, #0x40000000"
    "\n\tand     x1, x0, #0xff"
    "\n\tcsel    x1, x1, xzr, eq" // core number
    "\n\tadr     x0, initial_first_free_page"
    "\n\tmadd    x0, x1, x3, x0"
    "\n\tadd     sp, x0, x3"
    "\n\tb       c_el3_nommu"
    "\n.previous" );
