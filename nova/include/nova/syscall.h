/* -*- Mode: C++ -*- */

#pragma once

#include <nova/types.h>
#include <nova/utcb.h>

#ifndef __i386
# error Unknown platform.
#endif

NOVA_BEGIN

enum {
  // Syscalls
  CALL = 0,
  REPLY,			// XXX Hardcoded in assembler bindings.
  CREATE_PD,
  CREATE_EC,
  CREATE_SC,
  CREATE_PT,
  CREATE_SM,
  REVOKE,
  RECALL,
  SEMCTL
};

enum {
  // Flags
  NOBLOCK  = 1,
  NODONATE = 2,
  NOREPLY  = 4,
  // Implemented combinations
  DCALL    = 0,
  SEND     = NOREPLY | NODONATE,
};

enum {
  // Return values
  SUCCESS = 0,
  TIMEOUT,
  BAD_SYS,
  BAD_CAP,
  BAD_MEM,
  BAD_FTR,
};

NOVA_INLINE uint8_t
hypercall_1(uint8_t syscall_no, uint8_t flags, uint32_t word1)
{
  uint8_t result;

  asm volatile ("mov %%esp, %%ecx\n"
		"mov $0f, %%edx\n"
		"sysenter\n"
		"0:\n"
		: "=a" (result)		     // Output
		: "a" (syscall_no | (flags << 8)), // Input
		  "D" (word1)
		: "ecx", "edx"
		);

  return result;
}

NOVA_INLINE uint8_t
hypercall_2(uint8_t syscall_no, uint8_t flags, uint32_t word1, uint32_t word2)
{
  uint8_t result;

  asm volatile ("mov %%esp, %%ecx\n"
		"mov $0f, %%edx\n"
		"sysenter\n"
		"0:\n"
		: "=a" (result)		     // Output
		: "a" (syscall_no | (flags << 8)), // Input
		  "D" (word1),
		  "S" (word2)
		: "ecx", "edx"
		);

  return result;
}

NOVA_INLINE uint8_t
hypercall_3(uint8_t syscall_no, uint8_t flags, uint32_t word1,
	    uint32_t word2, uint32_t word3)
{
  uint8_t result;

  asm volatile ("mov %%esp, %%ecx\n"
		"mov $0f, %%edx\n"
		"sysenter\n"
		"0:\n"
		: "=a" (result)		     // Output
		: "a" (syscall_no | (flags << 8)), // Input
		  "D" (word1),
		  "S" (word2),
		  "b" (word3)
		: "ecx", "edx"
		);

  return result;
}

NOVA_INLINE uint8_t
hypercall_4(uint8_t syscall_no, uint8_t flags, uint32_t word1,
	    uint32_t word2, uint32_t word3, uint32_t word4)
{
  uint32_t dummy1;
  uint8_t result;

  asm ("push %%ebp\n" // XXX Always safe? ESP might be bogus.
       "mov %%ecx, %%ebp\n"
       "mov %%esp, %%ecx\n"
       "mov $0f, %%edx\n"
       "sysenter\n"
       "0: pop %%ebp\n"
       : "=a" (result),
	 "+c" (word4)
       : "a" (syscall_no | (flags << 8)),
	 "D" (word1),
	 "S" (word2),
	 "b" (word3)
       : "edx", "memory");

  return result;
}



NOVA_INLINE uint8_t call(uint32_t flags, Cap_idx pt, Mtd mtd) {
  return hypercall_2(CALL, flags, pt, mtd);
}

NOVA_INLINE uint8_t
reply(Mtd mtd, void *esp)
{
  uint8_t result;

  asm ("mov $0f, %%edx\n"
       "sysenter\n"
       "0:\n"
       : "=a" (result) // Output
       : "a" (REPLY), // Input
	 // Why is EDI left out? -> was used as conditional 
	 "S" (mtd),
	 "c" (esp)
       : "edx", "memory" // Clobbers
       );

  return result;
}

NOVA_INLINE uint8_t
create_pd(Cap_idx pd, Utcb *utcb, Qpd qpd, Cap_range obj, bool vm)
{
  return hypercall_4(CREATE_PT, vm ? 1 : 0, pd, NOVA_CAST(uint32_t, utcb), qpd, obj);
}

NOVA_INLINE uint8_t
create_ec(Cap_idx ec, Utcb *utcb, void *sp)
{
  return hypercall_3(CREATE_EC, 0, ec, NOVA_CAST(uint32_t, utcb), NOVA_CAST(uint32_t, sp));
}

NOVA_INLINE uint8_t
create_sc(Cap_idx sc, Cap_idx ec, Qpd qpd)
{
  return hypercall_3(CREATE_SC, 0, sc, ec, qpd);
}

NOVA_INLINE uint8_t
create_pt(Cap_idx pt, Cap_idx ec, Mtd mtd, uint32_t ip)
{
  return hypercall_4(CREATE_PT, 0, pt, ec, mtd, ip);
}

NOVA_INLINE uint8_t create_sm(Cap_idx sm, uint32_t count)
{
  return hypercall_2(CREATE_SM, 0, sm, count);
}

NOVA_INLINE uint8_t recall(Cap_idx ec)
{
  return hypercall_1(RECALL, 0, ec);
}

NOVA_INLINE uint8_t revoke(Cap_range caps, bool self)
{
  return hypercall_1(REVOKE, self ? 1 : 0, caps);
}

NOVA_INLINE uint8_t semup(Cap_idx sm)
{
  return hypercall_1(SEMCTL, 0, sm);
}

NOVA_INLINE uint8_t semdown(Cap_idx sm)
{
  return hypercall_1(SEMCTL, 1, sm);
}

NOVA_END

/* EOF */
