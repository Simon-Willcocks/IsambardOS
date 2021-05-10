#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef union {
  uint32_t raw;
  struct {
    uint32_t is_read:1;
    uint32_t CRm:4;
    uint32_t Rt:5;
    uint32_t CRn:4;
    uint32_t Opc1:3;
    uint32_t Opc2:3;
    uint32_t COND:4;
    uint32_t CV:1;
  };
} copro_syndrome;

int main( int argc, const char *argv[] )
{
  copro_syndrome cs = { .raw = atoi( argv[1] ) };
  printf( 
    "is_read: %d\n"
    "CRn: %d\n"
    "Opc1: %d\n"
    "CRm: %d\n"
    "Opc2: %d\n"
    "Rt: %d\n"
    "COND: %d\n"
    "CV: %d\n", 
    cs.is_read,
    cs.CRn,
    cs.Opc1,
    cs.CRm,
    cs.Opc2,
    cs.Rt,
    cs.COND,
    cs.CV );
  return 0;
}
