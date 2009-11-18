/* -*- Mode: C++ -*- */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <nova/compiler.h>

NOVA_BEGIN

typedef uint32_t Cap_idx;
typedef uint32_t Cap_range;
typedef uint32_t Mtd;
typedef uint32_t Qpd;

enum {
  CAP_R   = 1,
  CAP_W   = 2,
  CAP_X   = 4,
  CAP_RW  = CAP_R | CAP_W,
  CAP_RWX = CAP_RW | CAP_X
};

enum {
  MTD_NONE = 0,
  MTD_ABCD = (1 << 0),
  MTD_BSD  = (1 << 1),
  MTD_ESP  = (1 << 2),
  MTD_EIP  = (1 << 3),
  MTD_EFL  = (1 << 4),
  MTD_DSES = (1 << 5),
  MTD_FSGS = (1 << 6),
  MTD_CSSS = (1 << 7),
  MTD_TR   = (1 << 8),
  MTD_LDTR = (1 << 9),
  MTD_GDTR = (1 << 10),
  MTD_IDTR = (1 << 11),
  MTD_CR   = (1 << 12),
  MTD_DR   = (1 << 13),
  MTD_SYS  = (1 << 14),
  MTD_QUAL = (1 << 15),
  MTD_CTRL = (1 << 16),
  MTD_INJ  = (1 << 17),
  MTD_STA  = (1 << 18),
  MTD_TSC  = (1 << 19),
};

NOVA_INLINE Mtd empty_message() { return 0; }
NOVA_INLINE Mtd untyped_words(unsigned u) { return u; }
NOVA_INLINE Mtd typed_words(unsigned x) { return x << 23; }

NOVA_INLINE Cap_range mem_range(uint32_t address, uint8_t order, uint8_t access)
{
  return 1 | (access << 2) | (order << 7) | address;
}

NOVA_INLINE Cap_range io_range(uint16_t io_address, uint8_t order)
{
  return 2 | (order << 7) | (io_address << 12);
  }

NOVA_INLINE Cap_range obj_range(Cap_idx idx, uint8_t order)
{
    return 3 | (order << 7) | (idx << 12);
}

NOVA_INLINE Qpd make_qpd(uint8_t prio, uint32_t quantum)
{
  return prio | ( quantum << 12);
}

NOVA_END

/* EOF */
