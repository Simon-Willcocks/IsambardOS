// Copyright (c) Simon Willcocks 2019
//
// This code intentionally omits error checking

#ifndef AARCH64_VMSA_H
#define AARCH64_VMSA_H

static uint64_t const Aarch64_VMSA_Isambard_memory_attributes = 0xffbbf4440400ull;
// static uint64_t const Aarch64_VMSA_Isambard_memory_attributes = 0x444444440400ull;

// The values of this enum reference the bytes in Aarch64_VMSA_standard_memory_attributes
// Do not change one without the other.
enum Aarch64_VMSA_standard_memory_attribute_indexes { Device_nGnRnE = 0,
                                         Device_nGnRE = 1,
					 Non_cacheable = 2,
					 Inner_write_through = 3,
					 Outer_write_through = 4,
					 Fully_Cacheable = 5 };

typedef union {
  uint64_t raw;
  struct __attribute__(( packed )) {
    uint64_t type:2;
    uint64_t memory_attributes:4;
    uint64_t readable:1;
    uint64_t writable:1; // Allows for write-only memory!
    uint64_t shareability:2;
    uint64_t access_flag:1;
    uint64_t res0_1:1;
    uint64_t four_k_page_number:39;
    uint64_t dirty_bit_modifier:1;
    uint64_t contiguous:1;
    uint64_t privileged_execute_never:1;
    uint64_t unprivileged_execute_never:1;
    uint64_t reserved:4;
    uint64_t PHBA:4;
    uint64_t ignored:1;
  } level2;
  struct __attribute__(( packed )) {
    uint64_t type:2;
    enum Aarch64_VMSA_standard_memory_attribute_indexes memory_type:3;
    uint64_t not_secure:1;
    uint64_t el0_accessible:1;
    uint64_t read_only:1;
    uint64_t shareability:2;
    uint64_t access_flag:1;
    uint64_t not_global:1;
    uint64_t four_k_page_number:39;
    uint64_t dirty_bit_modifier:1;
    uint64_t contiguous:1;
    uint64_t privileged_execute_never:1;
    uint64_t unprivileged_execute_never:1;
    uint64_t reserved:4;
    uint64_t PHBA:4;
    uint64_t ignored:1;
  };
} Aarch64_VMSA_entry;

static Aarch64_VMSA_entry const Aarch64_VMSA_invalid = { .raw = 0 }; // Not the only invalid value, but guaranteed invalid.

static inline Aarch64_VMSA_entry Aarch64_VMSA_page_at( uint64_t physical )
{
  Aarch64_VMSA_entry result = { .raw = physical };
  result.type = 3;
  return result;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_subtable_at( Aarch64_VMSA_entry *physical )
{
  Aarch64_VMSA_entry result = { .raw = (uint64_t) physical };
  result.type = 3;
  return result;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_block_at( uint64_t physical )
{
  Aarch64_VMSA_entry result = { .raw = physical };
  result.type = 1;
  return result;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_device_memory( Aarch64_VMSA_entry entry )
{
  entry.memory_type = Device_nGnRnE;
  entry.privileged_execute_never = 1;
  entry.unprivileged_execute_never = 1;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_write_back_memory( Aarch64_VMSA_entry entry )
{
  entry.memory_type = Fully_Cacheable;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_inner_write_through_memory( Aarch64_VMSA_entry entry )
{
  entry.memory_type = Inner_write_through;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_outer_write_through_memory( Aarch64_VMSA_entry entry )
{
  entry.memory_type = Outer_write_through;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_uncached_memory( Aarch64_VMSA_entry entry )
{
  entry.memory_type = Non_cacheable;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_not_global( Aarch64_VMSA_entry entry )
{
  entry.not_global = 1;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_global( Aarch64_VMSA_entry entry )
{
  entry.not_global = 0;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_priv_ro_( Aarch64_VMSA_entry entry, int level )
{
  if (level == 1) entry.privileged_execute_never = 1;
  entry.unprivileged_execute_never = 1;
  entry.read_only = 1;
  entry.el0_accessible = 0;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_el0_ro_( Aarch64_VMSA_entry entry )
{
  entry.privileged_execute_never = 1;
  entry.unprivileged_execute_never = 1;
  entry.read_only = 1;
  entry.el0_accessible = 1;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_priv_rw_( Aarch64_VMSA_entry entry, int level )
{
  if (level == 1) entry.privileged_execute_never = 1;
  entry.unprivileged_execute_never = 1;
  entry.read_only = 0;
  entry.el0_accessible = 0;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_el0_rw_( Aarch64_VMSA_entry entry )
{
  entry.privileged_execute_never = 1;
  entry.unprivileged_execute_never = 1;
  entry.read_only = 0;
  entry.el0_accessible = 1;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_priv_r_x( Aarch64_VMSA_entry entry )
{
  entry.privileged_execute_never = 0;
  entry.unprivileged_execute_never = 0;
  entry.read_only = 1;
  entry.el0_accessible = 0;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_el0_r_x( Aarch64_VMSA_entry entry )
{
  entry.privileged_execute_never = 0;
  entry.unprivileged_execute_never = 0;
  entry.read_only = 1;
  entry.el0_accessible = 1;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_priv_rwx( Aarch64_VMSA_entry entry )
{
  entry.privileged_execute_never = 0;
  entry.unprivileged_execute_never = 0;
  entry.read_only = 0;
  entry.el0_accessible = 0;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_el0_rwx( Aarch64_VMSA_entry entry )
{
  entry.privileged_execute_never = 0;
  entry.unprivileged_execute_never = 0;
  entry.read_only = 0;
  entry.el0_accessible = 1;
  return entry;
}

static inline Aarch64_VMSA_entry Aarch64_VMSA_L2_rwx( Aarch64_VMSA_entry entry )
{
  entry.level2.privileged_execute_never = 0;
  entry.level2.unprivileged_execute_never = 0;
  entry.level2.readable = 1;
  entry.level2.writable = 1;
  return entry;
}

#endif
