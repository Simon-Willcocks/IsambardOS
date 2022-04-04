/* Copyright 2022 Simon Willcocks
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

typedef unsigned long long uint64_t;
typedef unsigned        uint32_t;
typedef int             int32_t;
typedef short           int16_t;
typedef signed char     int8_t;
typedef unsigned char   uint8_t;
typedef unsigned        size_t;
typedef unsigned        bool;
#define true  (0 == 0)
#define false (0 != 0)

#define number_of( arr ) (sizeof( arr ) / sizeof( arr[0] ))

static bool naturally_aligned( uint32_t p )
{
  return 0 == (p & ((2 << 20)-1));
}

uint32_t Cortex_A7_number_of_cores()
{
  uint32_t result;
  // L2CTLR, ARM DDI 0500G Cortex-A53, generally usable?
  asm ( "MRC p15, 1, %[result], c9, c0, 2" : [result] "=r" (result) );
  return ((result >> 24) & 3) + 1;
}

uint32_t pre_mmu_identify_processor()
{
  return Cortex_A7_number_of_cores();
}

inline uint32_t __attribute__(( always_inline )) get_core_number()
{
  uint32_t result;
  asm ( "mrc p15, 0, %[result], c0, c0, 5" : [result] "=r" (result) );
  return ((result & 0xc0000000) != 0x80000000) ? 0 : (result & 15);
}

typedef struct {
  uint32_t base;
  uint32_t size;
} ram_block;

// Various values that are needed pre-mmu
typedef struct {
  uint32_t relocation_offset;
  uint32_t final_location;
  uint32_t *states;
  bool states_initialised;
  uint32_t core_workspaces;
  uint32_t shared_memory;

  ram_block ram_blocks[4];

  ram_block less_aligned;
  uint32_t core_to_enter_mmu;
  uint32_t core_entered_mmu;
} startup;

extern startup boot_data; // Read-only once MMU enabled

// microclib

static inline int strlen( const char *string )
{
  int result = 0;
  while (*string++ != '\0') result++;
  return result;
}

static inline int strcmp( const char *left, const char *right )
{
  int result = 0;
  while (result == 0) {
    char l = *left++;
    char r = *right++;
    result = l - r;
    if (l == 0 || r == 0) break;
  }
  return result;
}

static inline char *strcpy( char *dest, const char *src )
{
  char *result = dest;
  while (*src != '\0') {
    *dest++ = *src++;
  }
  *dest = *src;
  return result;
}

