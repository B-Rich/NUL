/** @file
 * C++ Runtime Stubs
 *
 * Copyright (C) 2010, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of Vancouver.
 *
 * Vancouver is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vancouver is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

extern "C" void __cxa_pure_virtual(void) __attribute__((noreturn));
extern "C" void __cxa_pure_virtual(void) { __builtin_trap(); }

extern "C" int
__popcountsi2(unsigned int v)
{
  // I am itching to rewrite this in inline assembler using
  // shr/adc... ;-)
  unsigned n = 0;
  for ( ; v != 0; v >>= 1 )
    n += (v & 1);
  return n;
}

// EOF
