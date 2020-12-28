#! /bin/bash -

TARGET=aarch64-none-elf-

rm kernel8.dump kernel8.elf sdfat/kernel8.img

OPTIMISATION="-Os"

# Automatically allow for modifications to Core structure
core_size() {
( echo '#include "include/kernel.h"'; echo 'Core var = { 0 };' ) | ${TARGET}gcc -o /tmp/$$.o -x c - -c -DCORE_STACK_SIZE=$1 &&
${TARGET}objdump --disassemble-all /tmp/$$.o -x | sed -n 's/^.*bss[^0]*0*\(.*\) var$/0x\1/p' || ( rm /tmp/$$.o ; exit 1 )
rm /tmp/$$.o
}

if [ -z "$STACK_SIZE" ] ; then
  echo -n Calculating appropriate stack size...
  MIN_STACK_SIZE=400
  CORE_SIZE=$( core_size $MIN_STACK_SIZE )
  STACK_SIZE=$(( $MIN_STACK_SIZE + ( 0x1000 - $CORE_SIZE & 0xfff ) / 8 ))
fi

CORE_SIZE=$( core_size $STACK_SIZE )
test 0 -eq $(( 0xfff & $CORE_SIZE )) || ( echo Invalid stack size: $STACK_SIZE ; exit 1 )
echo done: $STACK_SIZE

echo Core size $CORE_SIZE

# -mgeneral-regs-only Stops the compiler using floating point registers as temprary storage
# -ffixed-x18 stops the compiler from using x18, so that it can be used to store the current thread (not trusted by the kernel, of course)
# I anticipate code with lots of fast locks, so a register is probably a better choice than TLS. ICBW.
CFLAGS="-I include -mgeneral-regs-only $OPTIMISATION -g -Wall -Wextra -fno-zero-initialized-in-bss -nostartfiles -nostdlib -T ld.script -mtune=cortex-a53 -DCORE_STACK_SIZE=$STACK_SIZE -ffixed-x18 "

KERNEL_ELEMENTS="boot.c el3.c memset.c"

${TARGET}gcc -o kernel8.elf $KERNEL_ELEMENTS $CFLAGS -DCORE_SIZE=\"$CORE_SIZE\" &&

${TARGET}objcopy kernel8.elf sdfat/kernel8.img -O binary &&
${TARGET}objdump -x --source --disassemble kernel8.elf > kernel8.dump &&
grep -vl \\.bss kernel8.dump || ( echo Uninitialised variables expand the kernel image unnecessarily && false )
