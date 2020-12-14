#! /bin/bash -

TARGET=aarch64-none-elf-

rm kernel8.*

OPTIMISATION="-Os"

# -mgeneral-regs-only Stops the compiler using floating point registers as temprary storage
CFLAGS="-mgeneral-regs-only -Dtrue='(0==0)' -Dfalse='(0!=0)' $OPTIMISATION -g -Wall -Wextra -fno-zero-initialized-in-bss -nostartfiles -nostdlib -T ld.script -mtune=cortex-a53"

CORE_SIZE=4096

test 0 -eq $(( 0xfff & $CORE_SIZE )) || ( echo Invalid stack size: $STACK_SIZE ; exit 1 )

echo Core size $CORE_SIZE

KERNEL_ELEMENTS="boot.c el3.c memset.c"

${TARGET}gcc -o kernel8.elf $KERNEL_ELEMENTS $CFLAGS -DCORE_SIZE=\"$CORE_SIZE\" &&

${TARGET}objcopy kernel8.elf kernel8.img -O binary &&
${TARGET}objdump -x --source --disassemble-all kernel8.elf > kernel8.dump &&
grep -vl \\.bss kernel8.dump || ( echo Uninitialised variables expand the kernel image unnecessarily && false )
