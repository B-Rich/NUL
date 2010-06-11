/*
 * Common code for NOVA programs.
 *
 * Copyright (C) 2010, Bernhard Kauer <bk@vmmon.org>
 *
 * This file is part of Vancouver.
 *
 * Vancouver.nova is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * Vancouver.nova is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */
#pragma once

#include <sys/utcb.h>

/**
 * A simple program that allows to get the UTCB pointer from the stack.
 */
struct BaseProgram {
  /* XXX Don't forget to change the initial stack in nul/program.h at
     __start */
  static const unsigned stack_size_shift = 12;
  static const unsigned stack_size = (1U << stack_size_shift);

  /**
   * Get the UTCB pointer from the top of the stack.
   */
  static Utcb *myutcb() { unsigned long esp; asm volatile ("mov %%esp, %0" : "=r"(esp));
    return *reinterpret_cast<Utcb **>( ((esp & ~(stack_size-1)) + stack_size - sizeof(void *)));
  };


  /**
   * Revoke all memory for a given virtual region.
   */
  static void revoke_all_mem(void *address, unsigned long size, unsigned rights, bool myself) {

    unsigned long page = reinterpret_cast<unsigned long>(address);
    size += page & 0xfff;
    page >>= 12;
    size = (size + 0xfff) >> 12;
    while (size) {
      unsigned order = Cpu::minshift(page, size);
      check0(revoke(Crd(page, order, rights | 1), myself));
      size -= 1 << order;
      page += 1 << order;
    }
  }
};


/**
 * A template to simplify saving the utcb.
 */
template <unsigned words>
class TemporarySave
{
  void *_ptr;
  unsigned long _data[words];

 public:
 TemporarySave(void *ptr) : _ptr(ptr) { memcpy(_data, _ptr, words*sizeof(unsigned long)); }
  ~TemporarySave() { memcpy(_ptr, _data, words*sizeof(unsigned long)); }
};
