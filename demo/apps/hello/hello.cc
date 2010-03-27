/*
 * Test application that burns CPU time.
 *
 * Copyright (C) 2010, Bernhard Kauer <bk@vmmon.org>
 *
 * This file is part of Vancouver.nova.
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

#include "sigma0/console.h"
#include "nul/program.h"

class Hello : public ProgramConsole
{
public:
  void run(Utcb *utcb, Hip *hip)
  {
    console_init("Hello");
    for (unsigned i=1; i; i++)
      Logging::printf("%8x Hello World!\n", i++);
  }
};

ASMFUNCS(Hello, NovaProgram);
