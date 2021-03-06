#! /bin/bash -

#if [ "x$QEMU" != "x" ]; then
  pushd unit_tests/virtualisation &&
  ./make_test_image > ../../include/arm32_code.h &&
  popd || exit 1
#fi

if [ ! -e tools/interfaces -o tools/interfaces.c -nt tools/interfaces ] ; then
  gcc tools/interfaces.c -o tools/interfaces && ln -sf interfaces tools/server && ln -sf interfaces tools/client || exit 1
fi

rm -f kernel8.dump kernel8.elf sdfat/kernel8.img built_drivers/* include/interfaces/client/* include/interfaces/provider/*
rm -f include/interfaces
mkdir -p include/interfaces/{client,provider}
mkdir built_drivers

(
  cd interfaces
  for i in *
  do
    echo Interface: $i
    ../tools/client $i > ../include/interfaces/client/$i.h
    ../tools/server $i > ../include/interfaces/provider/$i.h
  done
  cd ..
)

TARGET=aarch64-linux-gnu-
GCC=aarch64-linux-gnu-gcc-10

# Later versions of gcc insert calls to memset when optimising
OPTIMISATION="-Os memset.c"

# Automatically allow for modifications to Core structure
core_size() {
( echo '#include "include/kernel.h"'; echo 'Core core_var = { 0 };' ) | ${GCC} -o /tmp/$$.o -x c - -c -DCORE_STACK_SIZE=$1 -fno-zero-initialized-in-bss &&
${TARGET}objdump --disassemble-all /tmp/$$.o -x | sed -n 's/^.*data[^0]*0*\(.*\) core_var$/0x\1/p'
}

CFLAGS="-I include $OPTIMISATION"

# Bare metal
CFLAGS+=" -nostartfiles -nostdlib -nostdinc -static "

# Just the right amount of warnings
CFLAGS+=" -Wall -Wextra -Wno-unused-function "

# No floating point registers in kernel code, and reserve x18 for task ID
# (not trusted by the kernel, of course)
# I anticipate code with lots of fast locks, so a register is probably a
# better choice than TLS. ICBW.
CFLAGS+=" -mgeneral-regs-only -ffixed-x18 "

# No loader needed, please. (No .got, .got.plt, .bss sections)
CFLAGS+=" -fno-zero-initialized-in-bss -fPIE -fpie "

# Where are we going to run it?
CFLAGS+=" -mtune=cortex-a53 $QEMU "

# Linker flags
CFLAGS+=" -Wl,--build-id=none,-no-dynamic-linker"

# We do not want a GLOBAL_OFFSET_TABLE (if using a aarch64-linux-...-gcc, the
# default is -fPIC)
CFLAGS+=" -fno-PIC"

echo CFLAGS: $CFLAGS

#KERNEL_ELEMENTS="boot.c el3.c el3_gpio4_debug.c secure_el1.c kernel_translation_tables.c"
KERNEL_ELEMENTS="boot.c el3.c el3_virtual_machines.c el3_bsod.c secure_el1.c kernel_translation_tables.c"

SYSTEM_DRIVER=system
MEMORY_DRIVER=physical_memory_allocator
SPECIAL_DRIVERS="$SYSTEM_DRIVER $MEMORY_DRIVER"

DRIVERS="pi3gpu show_page riscos"
#DRIVERS="test_vm"

echo Building drivers: $DRIVERS

count() { echo -n $#; }

symbol() {
  ${TARGET}objdump -t $1 | sed -n 's/^\([0-9a-f]*\).*\<'$2'\>/0x\1/p'
}

build_driver() {
  echo Building $2
  # Each driver's code and data is padded to a 4k boundary, the .bin file will be a multiple of 4k in size.
  # Parameters: ld.script for build (used to locate code other than at 0), driver name

  # The system driver has its own entry protocol, disable the libdrivers.c init code
  if [ "$2" = "$SYSTEM_DRIVER" ] ; then
    SYSTEM_DEFINE="-DSYSTEM_DRIVER"
  else
    SYSTEM_DEFINE=""
  fi

  if [ -d drivers/"$2" ] ; then
    ${GCC} -g -I drivers/"$2" drivers/libdrivers.c drivers/"$2"/*.c -o built_drivers/"$2".elf -T $1 $CFLAGS
  else
    ${GCC} -g drivers/libdrivers.c drivers/"$2".c -o built_drivers/"$2".elf -T $1 $CFLAGS $SYSTEM_DEFINE
  fi &&
  # elf object file to binary, runnable code and data
  ${TARGET}objcopy -O binary built_drivers/"$2".elf built_drivers/"$2".bin --gap-fill 42 --pad-to $(( $( symbol built_drivers/"$2".elf pad_to_here ) )) &&
  # Binary file to object file that can be linked into kernel
  ${TARGET}objcopy -I binary -O elf64-littleaarch64 -B aarch64 built_drivers/"$2".bin built_drivers/"$2".o --rename-section .data=.data_driver,alloc,load,readonly,data,contents \
    --add-symbol '_binary_built_drivers_'$2'_data_pages='$(( $( symbol built_drivers/"$2".elf data_pages ) )) \
    --add-symbol '_binary_built_drivers_'$2'_code_pages='$(( $( symbol built_drivers/"$2".elf code_pages ) )) &&
  ${TARGET}objdump -x --source --disassemble built_drivers/"$2".elf > built_drivers/"$2".dump &&
  echo Built $2
}

build_driver system_driver.ld.script "$SYSTEM_DRIVER" &&

build_driver memory_driver.ld.script "$MEMORY_DRIVER" &&

for driver in $DRIVERS
do
  echo $driver
  build_driver driver.ld.script "$driver" || exit 1
done

(
echo '// Automatically generated file, do not check in to git.'
echo '#include "types.h"'
echo
for driver in $SYSTEM_DRIVER $MEMORY_DRIVER $DRIVERS
do
  echo 'extern uint8_t _binary_built_drivers_'$driver'_bin_start;'
  echo 'extern uint8_t _binary_built_drivers_'$driver'_bin_end;'
  echo 'extern uint8_t _binary_built_drivers_'$driver'_code_pages;'
  echo 'extern uint8_t _binary_built_drivers_'$driver'_data_pages;'
done
echo
echo 'struct {'
echo '  integer_register start;'
echo '  integer_register end;'
echo '  integer_register code_pages;'
echo '  integer_register data_pages;'
echo '} const drivers[] = {'
for driver in $SYSTEM_DRIVER $MEMORY_DRIVER $DRIVERS
do
  if [ $driver != $SYSTEM_DRIVER ]; then echo "  ," ; fi
  echo '  {'
  echo '    .start = &_binary_built_drivers_'$driver'_bin_start - (uint8_t*) 0,'
  echo '    .end = &_binary_built_drivers_'$driver'_bin_end - (uint8_t*) 0,'
  echo '    .code_pages = &_binary_built_drivers_'$driver'_code_pages - (uint8_t*) 0,'
  echo '    .data_pages = &_binary_built_drivers_'$driver'_data_pages - (uint8_t*) 0'
  echo '  }'
done
echo '};'
) > built_drivers/drivers_info.h

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

echo Compiling kernel
${GCC} -o kernel8.elf -T ld.script -I built_drivers/ $KERNEL_ELEMENTS built_drivers/*.o $CFLAGS -DCORE_SIZE=\"$CORE_SIZE\" -DCORE_STACK_SIZE=$STACK_SIZE &&
echo compiled kernel OK &&

${TARGET}objcopy kernel8.elf sdfat/kernel8.img -O binary &&
echo ObjCopy OK &&
${TARGET}objdump -x --source --disassemble-all kernel8.elf > kernel8.dump &&
echo ObjDump OK &&

# Check for common mistakes in the build or code
echo Checking that all variables are initialised in the kernel files &&
( grep -vl \\.bss kernel8.dump || ( echo Uninitialised variables expand the kernel image unnecessarily && false ) ) &&
echo Checking that the drivers are properly padded to whole pages &&
grep ' _binary_built_.*bin_' kernel8.dump | grep -e "[1-9a-f][0-9a-f][0-9a-f] g" -e "[0-9a-f][1-9a-f][0-9a-f] g" -e "[0-9a-f][0-9a-f][1-9a-f] g" && ( echo Build failure, drivers not aligned properly ; exit 1 )
echo Checking that all global or local variables with the word \"stack\" at the end of the name are 16-byte aligned &&
grep -e '^000.*stack\>' -e '^000.*stack\.[0-9]*\>' {,built_drivers/}*.dump | grep '[1-9a-f] g' && ( echo Possible unaligned stacks? ; exit 1 ) || true

